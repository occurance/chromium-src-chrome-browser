// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/glue/synced_tab_delegate_android.h"

#include "base/memory/ref_counted.h"
#include "chrome/browser/android/tab_android.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/sync/glue/synced_window_delegate.h"
#include "chrome/browser/ui/sync/tab_contents_synced_tab_delegate.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

using content::NavigationEntry;

SyncedTabDelegateAndroid::SyncedTabDelegateAndroid(TabAndroid* tab_android)
    : web_contents_(NULL), tab_android_(tab_android) {}

SyncedTabDelegateAndroid::~SyncedTabDelegateAndroid() {}

SessionID::id_type SyncedTabDelegateAndroid::GetWindowId() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetWindowId();
}

SessionID::id_type SyncedTabDelegateAndroid::GetSessionId() const {
  return tab_android_->id().id();
}

bool SyncedTabDelegateAndroid::IsBeingDestroyed() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->IsBeingDestroyed();
}

Profile* SyncedTabDelegateAndroid::profile() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->profile();
}

std::string SyncedTabDelegateAndroid::GetExtensionAppId() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetExtensionAppId();
}

int SyncedTabDelegateAndroid::GetCurrentEntryIndex() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetCurrentEntryIndex();
}

int SyncedTabDelegateAndroid::GetEntryCount() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetEntryCount();
}

int SyncedTabDelegateAndroid::GetPendingEntryIndex() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetPendingEntryIndex();
}

NavigationEntry* SyncedTabDelegateAndroid::GetPendingEntry() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetPendingEntry();
}

NavigationEntry* SyncedTabDelegateAndroid::GetEntryAtIndex(int i) const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetEntryAtIndex(i);
}

NavigationEntry* SyncedTabDelegateAndroid::GetActiveEntry() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetActiveEntry();
}

bool SyncedTabDelegateAndroid::IsPinned() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->IsPinned();
}

bool SyncedTabDelegateAndroid::HasWebContents() const {
  return web_contents_ != NULL;
}

void SyncedTabDelegateAndroid::SetWebContents(
    content::WebContents* web_contents) {
  web_contents_ = web_contents;
  TabContentsSyncedTabDelegate::CreateForWebContents(web_contents_);
}

void SyncedTabDelegateAndroid::ResetWebContents() { web_contents_ = NULL; }

bool SyncedTabDelegateAndroid::ProfileIsManaged() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->ProfileIsManaged();
}

const std::vector<const content::NavigationEntry*>*
SyncedTabDelegateAndroid::GetBlockedNavigations() const {
  return TabContentsSyncedTabDelegate::FromWebContents(web_contents_)
      ->GetBlockedNavigations();
}