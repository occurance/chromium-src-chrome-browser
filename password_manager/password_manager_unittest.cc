// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/message_loop.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/password_manager/mock_password_store.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/password_manager/password_manager_delegate.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/password_manager/password_store_factory.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/browser/tab_contents/test_tab_contents.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/common/frame_navigate_params.h"
#include "content/test/test_browser_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using webkit::forms::PasswordForm;
using testing::_;
using testing::DoAll;
using ::testing::Exactly;
using ::testing::WithArg;
using ::testing::Return;

class MockPasswordManagerDelegate : public PasswordManagerDelegate {
 public:
  MOCK_METHOD1(FillPasswordForm, void(
     const webkit::forms::PasswordFormFillData&));
  MOCK_METHOD1(AddSavePasswordInfoBar, void(PasswordFormManager*));
  MOCK_METHOD0(GetProfileForPasswordManager, Profile*());
  MOCK_METHOD0(DidLastPageLoadEncounterSSLErrors, bool());
};

ACTION_P2(InvokeConsumer, handle, forms) {
  arg0->OnPasswordStoreRequestDone(handle, forms);
}

ACTION_P(SaveToScopedPtr, scoped) {
  scoped->reset(arg0);
}

class PasswordManagerTest : public ChromeRenderViewHostTestHarness {
 public:
  PasswordManagerTest()
      : ui_thread_(BrowserThread::UI, MessageLoopForUI::current()) {}
 protected:

  virtual void SetUp() {
    TestingProfile* testing_profile = new TestingProfile;
    store_ = static_cast<MockPasswordStore*>(
        PasswordStoreFactory::GetInstance()->SetTestingFactoryAndUse(
            testing_profile, MockPasswordStore::Build));
    browser_context_.reset(testing_profile);
    ChromeRenderViewHostTestHarness::SetUp();

    EXPECT_CALL(delegate_, GetProfileForPasswordManager())
        .WillRepeatedly(Return(profile()));
    manager_.reset(new PasswordManager(contents(), &delegate_));
    EXPECT_CALL(delegate_, DidLastPageLoadEncounterSSLErrors())
        .WillRepeatedly(Return(false));
  }

  virtual void TearDown() {
    manager_.reset();
    store_ = NULL;
    ChromeRenderViewHostTestHarness::TearDown();
  }

  PasswordForm MakeSimpleForm() {
    PasswordForm form;
    form.origin = GURL("http://www.google.com/a/LoginAuth");
    form.action = GURL("http://www.google.com/a/Login");
    form.username_element = ASCIIToUTF16("Email");
    form.password_element = ASCIIToUTF16("Passwd");
    form.username_value = ASCIIToUTF16("google");
    form.password_value = ASCIIToUTF16("password");
    form.submit_element = ASCIIToUTF16("signIn");
    form.signon_realm = "http://www.google.com";
    return form;
  }

  PasswordManager* manager() { return manager_.get(); }

  // We create a UI thread to satisfy PasswordStore.
  content::TestBrowserThread ui_thread_;

  scoped_refptr<MockPasswordStore> store_;
  MockPasswordManagerDelegate delegate_;  // Owned by manager_.
  scoped_ptr<PasswordManager> manager_;
};

MATCHER_P(FormMatches, form, "") {
  return form.signon_realm == arg.signon_realm &&
         form.origin == arg.origin &&
         form.action == arg.action &&
         form.username_element == arg.username_element &&
         form.password_element == arg.password_element &&
         form.submit_element == arg.submit_element;
}

TEST_F(PasswordManagerTest, FormSubmitEmptyStore) {
  // Test that observing a newly submitted form shows the save password bar.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_, GetLogins(_,_))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.

  // And the form submit contract is to call ProvisionallySavePassword.
  manager()->ProvisionallySavePassword(form);

  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBar(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  // Now the password manager waits for the navigation to complete.
  manager()->DidStopLoading();

  ASSERT_FALSE(NULL == form_to_save.get());
  EXPECT_CALL(*store_, AddLogin(FormMatches(form)));

  // Simulate saving the form, as if the info bar was accepted.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, FormSubmitNoGoodMatch) {
  // Same as above, except with an existing form for the same signon realm,
  // but different origin.  Detailed cases like this are covered by
  // PasswordFormManagerTest.
  std::vector<PasswordForm*> result;
  PasswordForm* existing_different = new PasswordForm(MakeSimpleForm());
  existing_different->username_value = ASCIIToUTF16("google2");
  result.push_back(existing_different);
  EXPECT_CALL(delegate_, FillPasswordForm(_));
  EXPECT_CALL(*store_, GetLogins(_,_))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));

  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.
  manager()->ProvisionallySavePassword(form);

  // We still expect an add, since we didn't have a good match.
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBar(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  manager()->DidStopLoading();

  EXPECT_CALL(*store_, AddLogin(FormMatches(form)));
  // Simulate saving the form.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, FormSeenThenLeftPage) {
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_, GetLogins(_,_))
    .WillOnce(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.

  PasswordForm empty_form(form);
  empty_form.username_value = string16();
  empty_form.password_value = string16();
  content::LoadCommittedDetails details;
  content::FrameNavigateParams params;
  params.password_form = empty_form;
  manager()->DidNavigateAnyFrame(details, params);

  // No expected calls.
  EXPECT_CALL(delegate_, AddSavePasswordInfoBar(_)).Times(0);
  manager()->DidStopLoading();
}

TEST_F(PasswordManagerTest, FormSubmitAfterNavigateSubframe) {
  // Test that navigating a subframe does not prevent us from showing the save
  // password infobar.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_, GetLogins(_,_))
      .WillOnce(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.

  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBar(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  // Simulate navigating a sub-frame.
  content::LoadCommittedDetails details;
  content::FrameNavigateParams params;
  manager()->DidNavigateAnyFrame(details, params);

  // Simulate navigating the real page.
  params.password_form = form;
  manager()->DidNavigateAnyFrame(details, params);

  // Now the password manager waits for the navigation to complete.
  manager()->DidStopLoading();

  ASSERT_FALSE(NULL == form_to_save.get());
  EXPECT_CALL(*store_, AddLogin(FormMatches(form)));

  // Simulate saving the form, as if the info bar was accepted.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, FormSubmitFailedLogin) {
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_, GetLogins(_,_))
    .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.

  manager()->ProvisionallySavePassword(form);

  // The form reappears, and is visible in the layout:
  manager()->OnPasswordFormsFound(observed);
  manager()->OnPasswordFormsVisible(observed);

  // No expected calls to the PasswordStore...
  manager()->DidStopLoading();
}

TEST_F(PasswordManagerTest, FormSubmitInvisibleLogin) {
  // Tests fix of issue 28911: if the login form reappears on the subsequent
  // page, but is invisible, it shouldn't count as a failed login.
  std::vector<PasswordForm*> result;  // Empty password store.
  EXPECT_CALL(delegate_, FillPasswordForm(_)).Times(Exactly(0));
  EXPECT_CALL(*store_, GetLogins(_,_))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  manager()->OnPasswordFormsVisible(observed);  // The initial layout.

  manager()->ProvisionallySavePassword(form);

  // The form reappears, but is not visible in the layout:
  manager()->OnPasswordFormsFound(observed);
  // No call to PasswordFormsVisible.

  // Expect info bar to appear:
  scoped_ptr<PasswordFormManager> form_to_save;
  EXPECT_CALL(delegate_, AddSavePasswordInfoBar(_))
      .WillOnce(WithArg<0>(SaveToScopedPtr(&form_to_save)));

  manager()->DidStopLoading();

  ASSERT_FALSE(NULL == form_to_save.get());
  EXPECT_CALL(*store_, AddLogin(FormMatches(form)));
  // Simulate saving the form.
  form_to_save->Save();
}

TEST_F(PasswordManagerTest, InitiallyInvisibleForm) {
  // Make sure an invisible login form still gets autofilled.
  std::vector<PasswordForm*> result;
  PasswordForm* existing = new PasswordForm(MakeSimpleForm());
  result.push_back(existing);
  EXPECT_CALL(delegate_, FillPasswordForm(_));
  EXPECT_CALL(*store_, GetLogins(_,_))
      .WillRepeatedly(DoAll(WithArg<1>(InvokeConsumer(0, result)), Return(0)));
  std::vector<PasswordForm> observed;
  PasswordForm form(MakeSimpleForm());
  observed.push_back(form);
  manager()->OnPasswordFormsFound(observed);  // The initial load.
  // PasswordFormsVisible is not called.

  manager()->DidStopLoading();
}
