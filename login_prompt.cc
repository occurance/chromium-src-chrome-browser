// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/login_prompt.h"

#include <vector>

#include "app/l10n_util.h"
#include "base/command_line.h"
#include "base/lock.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/password_manager/password_manager.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host_request_info.h"
#include "chrome/browser/tab_contents/constrained_window.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "grit/generated_resources.h"
#include "net/base/auth.h"
#include "net/base/net_util.h"
#include "net/url_request/url_request.h"

using webkit_glue::PasswordForm;

class LoginHandlerImpl;

// Helper to remove the ref from an URLRequest to the LoginHandler.
// Should only be called from the IO thread, since it accesses an URLRequest.
void ResetLoginHandlerForRequest(URLRequest* request) {
  ResourceDispatcherHostRequestInfo* info =
      ResourceDispatcherHost::InfoForRequest(request);
  if (!info)
    return;

  info->set_login_handler(NULL);
}

// Get the signon_realm under which this auth info should be stored.
//
// The format of the signon_realm for proxy auth is:
//     proxy-host/auth-realm
// The format of the signon_realm for server auth is:
//     url-scheme://url-host[:url-port]/auth-realm
//
// Be careful when changing this function, since you could make existing
// saved logins un-retrievable.
std::string GetSignonRealm(const GURL& url,
                           const net::AuthChallengeInfo& auth_info) {
  std::string signon_realm;
  if (auth_info.is_proxy) {
    signon_realm = WideToASCII(auth_info.host_and_port);
    signon_realm.append("/");
  } else {
    // Take scheme, host, and port from the url.
    signon_realm = url.GetOrigin().spec();
    // This ends with a "/".
  }
  signon_realm.append(WideToUTF8(auth_info.realm));
  return signon_realm;
}

// ----------------------------------------------------------------------------
// LoginHandler

LoginHandler::LoginHandler(URLRequest* request)
    : request_(request),
      password_manager_(NULL),
      login_model_(NULL),
      handled_auth_(false),
      dialog_(NULL) {
  // This constructor is called on the I/O thread, so we cannot load the nib
  // here. BuildViewForPasswordManager() will be invoked on the UI thread
  // later, so wait with loading the nib until then.
  DCHECK(request_) << "LoginHandlerMac constructed with NULL request";
  DCHECK(ResourceDispatcherHost::RenderViewForRequest(
             request_, &render_process_host_id_,  &tab_contents_id_));
  AddRef();  // matched by ReleaseSoon::ReleaseSoon().
}

LoginHandler::~LoginHandler() {
}

void LoginHandler::SetPasswordForm(const webkit_glue::PasswordForm& form) {
  password_form_ = form;
}

void LoginHandler::SetPasswordManager(PasswordManager* password_manager) {
  password_manager_ = password_manager;
}

TabContents* LoginHandler::GetTabContentsForLogin() const {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  return tab_util::GetTabContentsByID(render_process_host_id_,
                                      tab_contents_id_);
}

void LoginHandler::SetAuth(const std::wstring& username,
                           const std::wstring& password) {
  if (WasAuthHandled(true))
    return;

  // Tell the password manager the credentials were submitted / accepted.
  if (password_manager_) {
    password_form_.username_value = WideToUTF16Hack(username);
    password_form_.password_value = WideToUTF16Hack(password);
    password_manager_->ProvisionallySavePassword(password_form_);
  }

  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::CloseContentsDeferred));
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::SendNotifications));
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::SetAuthDeferred,
                        username, password));
}

void LoginHandler::CancelAuth() {
  if (WasAuthHandled(true))
    return;

  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::CloseContentsDeferred));
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::SendNotifications));
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(this, &LoginHandler::CancelAuthDeferred));
}

void LoginHandler::OnRequestCancelled() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO)) <<
  "Why is OnRequestCancelled called from the UI thread?";

  // Reference is no longer valid.
  request_ = NULL;

  // Give up on auth if the request was cancelled.
  CancelAuth();
}

void LoginHandler::SetModel(LoginModel* model) {
  if (login_model_)
    login_model_->SetObserver(NULL);
  login_model_ = model;
  if (login_model_)
    login_model_->SetObserver(this);
}

void LoginHandler::SetDialog(ConstrainedWindow* dialog) {
  dialog_ = dialog;
}

// Notify observers that authentication is needed or received.  The automation
// proxy uses this for testing.
void LoginHandler::SendNotifications() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  NotificationService* service = NotificationService::current();
  TabContents* requesting_contents = GetTabContentsForLogin();
  if (!requesting_contents)
    return;

  NavigationController* controller = &requesting_contents->controller();

  if (!WasAuthHandled(false)) {
    LoginNotificationDetails details(this);
    service->Notify(NotificationType::AUTH_NEEDED,
                    Source<NavigationController>(controller),
                    Details<LoginNotificationDetails>(&details));
  } else {
    service->Notify(NotificationType::AUTH_SUPPLIED,
                    Source<NavigationController>(controller),
                    NotificationService::NoDetails());
  }
}

void LoginHandler::ReleaseSoon() {
  if (!WasAuthHandled(true)) {
    ChromeThread::PostTask(
        ChromeThread::IO, FROM_HERE,
        NewRunnableMethod(this, &LoginHandler::CancelAuthDeferred));
    ChromeThread::PostTask(
        ChromeThread::UI, FROM_HERE,
        NewRunnableMethod(this, &LoginHandler::SendNotifications));
  }

  // Delete this object once all InvokeLaters have been called.
  ChromeThread::ReleaseSoon(ChromeThread::IO, FROM_HERE, this);
}

// Returns whether authentication had been handled (SetAuth or CancelAuth).
// If |set_handled| is true, it will mark authentication as handled.
bool LoginHandler::WasAuthHandled(bool set_handled) {
  AutoLock lock(handled_auth_lock_);
  bool was_handled = handled_auth_;
  if (set_handled)
    handled_auth_ = true;
  return was_handled;
}

// Calls SetAuth from the IO loop.
void LoginHandler::SetAuthDeferred(const std::wstring& username,
                                   const std::wstring& password) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));

  if (request_) {
    request_->SetAuth(username, password);
    ResetLoginHandlerForRequest(request_);
  }
}

// Calls CancelAuth from the IO loop.
void LoginHandler::CancelAuthDeferred() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));

  if (request_) {
    request_->CancelAuth();
    // Verify that CancelAuth does destroy the request via our delegate.
    DCHECK(request_ != NULL);
    ResetLoginHandlerForRequest(request_);
  }
}

// Closes the view_contents from the UI loop.
void LoginHandler::CloseContentsDeferred() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));

  // The hosting ConstrainedWindow may have been freed.
  if (dialog_)
    dialog_->CloseConstrainedWindow();
}

// ----------------------------------------------------------------------------
// LoginDialogTask

// This task is run on the UI thread and creates a constrained window with
// a LoginView to prompt the user.  The response will be sent to LoginHandler,
// which then routes it to the URLRequest on the I/O thread.
class LoginDialogTask : public Task {
 public:
  LoginDialogTask(const GURL& request_url,
                  net::AuthChallengeInfo* auth_info,
                  LoginHandler* handler)
      : request_url_(request_url), auth_info_(auth_info), handler_(handler) {
  }
  virtual ~LoginDialogTask() {
  }

  void Run() {
    TabContents* parent_contents = handler_->GetTabContentsForLogin();
    if (!parent_contents) {
      // The request was probably cancelled.
      return;
    }

    // Tell the password manager to look for saved passwords.
    PasswordManager* password_manager =
        parent_contents->GetPasswordManager();
    std::vector<PasswordForm> v;
    MakeInputForPasswordManager(&v);
    password_manager->PasswordFormsSeen(v);
    handler_->SetPasswordManager(password_manager);

    std::wstring explanation = auth_info_->realm.empty() ?
        l10n_util::GetStringF(IDS_LOGIN_DIALOG_DESCRIPTION_NO_REALM,
                              auth_info_->host_and_port) :
        l10n_util::GetStringF(IDS_LOGIN_DIALOG_DESCRIPTION,
                              auth_info_->host_and_port,
                              auth_info_->realm);
    handler_->BuildViewForPasswordManager(password_manager,
                                          explanation);
  }

 private:
  // Helper to create a PasswordForm and stuff it into a vector as input
  // for PasswordManager::PasswordFormsSeen, the hook into PasswordManager.
  void MakeInputForPasswordManager(
      std::vector<PasswordForm>* password_manager_input) {
    PasswordForm dialog_form;
    if (LowerCaseEqualsASCII(auth_info_->scheme, "basic")) {
      dialog_form.scheme = PasswordForm::SCHEME_BASIC;
    } else if (LowerCaseEqualsASCII(auth_info_->scheme, "digest")) {
      dialog_form.scheme = PasswordForm::SCHEME_DIGEST;
    } else {
      dialog_form.scheme = PasswordForm::SCHEME_OTHER;
    }
    std::string host_and_port(WideToASCII(auth_info_->host_and_port));
    if (net::GetHostAndPort(request_url_) != host_and_port) {
      dialog_form.origin = GURL();
      NOTREACHED();
    } else if (auth_info_->is_proxy) {
      std::string origin = host_and_port;
      // We don't expect this to already start with http:// or https://.
      DCHECK(origin.find("http://") != 0 && origin.find("https://") != 0);
      origin = std::string("http://") + origin;
      dialog_form.origin = GURL(origin);
    } else {
      dialog_form.origin = GURL(request_url_.scheme() + "://" + host_and_port);
    }
    dialog_form.signon_realm = GetSignonRealm(dialog_form.origin, *auth_info_);
    password_manager_input->push_back(dialog_form);
    // Set the password form for the handler (by copy).
    handler_->SetPasswordForm(dialog_form);
  }

  // The url from the URLRequest initiating the auth challenge.
  GURL request_url_;

  // Info about who/where/what is asking for authentication.
  scoped_refptr<net::AuthChallengeInfo> auth_info_;

  // Where to send the authentication when obtained.
  // This is owned by the ResourceDispatcherHost that invoked us.
  LoginHandler* handler_;

  DISALLOW_EVIL_CONSTRUCTORS(LoginDialogTask);
};

// ----------------------------------------------------------------------------
// Public API

LoginHandler* CreateLoginPrompt(net::AuthChallengeInfo* auth_info,
                                URLRequest* request) {
  LoginHandler* handler = LoginHandler::Create(request);
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE, new LoginDialogTask(
          request->url(), auth_info, handler));
  return handler;
}
