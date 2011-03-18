// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This class is the (hackish) way to use the XMPP parts of
// MediatorThread for server-issued notifications.
//
// TODO(akalin): Decomp MediatorThread into an XMPP service part and a
// notifications-specific part and use the XMPP service part for
// server-issued notifications.

#ifndef CHROME_BROWSER_SYNC_NOTIFIER_SERVER_NOTIFIER_THREAD_H_
#define CHROME_BROWSER_SYNC_NOTIFIER_SERVER_NOTIFIER_THREAD_H_
#pragma once

#include <string>
#include <vector>

#include "base/observer_list_threadsafe.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/sync/notifier/chrome_invalidation_client.h"
#include "chrome/browser/sync/notifier/state_writer.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "jingle/notifier/listener/mediator_thread_impl.h"

namespace notifier {
struct NotifierOptions;
}

namespace sync_notifier {

class ServerNotifierThread
    : public notifier::MediatorThreadImpl,
      public ChromeInvalidationClient::Listener,
      public StateWriter {
 public:
  // Does not take ownership of |state_writer| (which may not
  // be NULL).
  explicit ServerNotifierThread(
      const notifier::NotifierOptions& notifier_options,
      const std::string& client_info,
      const std::string& state, StateWriter* state_writer);

  virtual ~ServerNotifierThread();

  // Overridden to start listening to server notifications.
  virtual void ListenForUpdates();

  // Overridden to immediately notify the delegate that subscriptions
  // (i.e., notifications) are on.  Must be called only after a call
  // to ListenForUpdates().
  virtual void SubscribeForUpdates(
      const notifier::SubscriptionList& subscriptions);

  // Overridden to stop listening to server notifications.
  virtual void Logout();

  virtual void SendNotification(const notifier::Notification& data);

  // ChromeInvalidationClient::Listener implementation.
  // We pass on two pieces of information to observers through the
  // Notification.
  // - the model type being invalidated, through the Notification's
  //       |channel| member.
  // - the invalidation payload for that model type, through the
  //       Notification's |data| member.
  virtual void OnInvalidate(syncable::ModelType model_type,
                            const std::string& payload);
  virtual void OnInvalidateAll();

  // StateWriter implementation.
  virtual void WriteState(const std::string& state);

  virtual void UpdateEnabledTypes(const syncable::ModelTypeSet& types);

 private:
  // Posted to the worker thread by ListenForUpdates().
  void DoListenForUpdates();

  // Posted to the worker thread by SubscribeForUpdates().
  void RegisterTypes();

  void SignalSubscribed();

  // Posted to the worker thread by Logout().
  void StopInvalidationListener();

  const std::string client_info_;
  std::string state_;
  // Hack to get the nice thread-safe behavior for |state_writer_|.
  scoped_refptr<ObserverListThreadSafe<StateWriter> > state_writers_;
  // We still need to keep |state_writer_| around to remove it from
  // |state_writers_|.
  StateWriter* state_writer_;
  scoped_ptr<ChromeInvalidationClient> chrome_invalidation_client_;

  syncable::ModelTypeSet registered_types_;

  void SetRegisteredTypes(const syncable::ModelTypeSet& types);
};

}  // namespace sync_notifier

DISABLE_RUNNABLE_METHOD_REFCOUNT(sync_notifier::ServerNotifierThread);

#endif  // CHROME_BROWSER_SYNC_NOTIFIER_SERVER_NOTIFIER_THREAD_H_
