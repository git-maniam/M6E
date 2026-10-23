/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IPC_GLUE_MESSAGEPUMP_H_
#define IPC_GLUE_MESSAGEPUMP_H_

#include "base/message_pump_default.h"
#  include "base/message_pump_mac.h"

#include "base/time.h"
#include "mozilla/Mutex.h"
#include "nsCOMPtr.h"
#include "nsIThreadInternal.h"

class nsIEventTarget;
class nsITimer;

namespace mozilla {
namespace ipc {

class DoWorkRunnable;

class MessagePump : public base::MessagePumpDefault {
  friend class DoWorkRunnable;

 public:
  explicit MessagePump(nsISerialEventTarget* aEventTarget);

  // From base::MessagePump.
  virtual void Run(base::MessagePump::Delegate* aDelegate) override;

  // From base::MessagePump.
  virtual void ScheduleWork() override;

  // From base::MessagePump.
  virtual void ScheduleWorkForNestedLoop() override;

  // From base::MessagePump.
  virtual void ScheduleDelayedWork(
      const base::TimeTicks& aDelayedWorkTime) override;

  virtual nsISerialEventTarget* GetXPCOMThread() override;

 protected:
  virtual ~MessagePump();

 private:
  // Only called by DoWorkRunnable.
  void DoDelayedWork(base::MessagePump::Delegate* aDelegate);

 protected:
  nsISerialEventTarget* mEventTarget;

  // mDelayedWorkTimer and mEventTarget are set in Run() by this class or its
  // subclasses.
  nsCOMPtr<nsITimer> mDelayedWorkTimer;

 private:
  // Only accessed by this class.
  RefPtr<DoWorkRunnable> mDoWorkEvent;
};

class MessagePumpForChildProcess final : public MessagePump {
 public:
  MessagePumpForChildProcess() : MessagePump(nullptr), mFirstRun(true) {}

  virtual void Run(base::MessagePump::Delegate* aDelegate) override;

 private:
  ~MessagePumpForChildProcess() = default;

  bool mFirstRun;
};

class MessagePumpForNonMainThreads final : public MessagePump {
 public:
  explicit MessagePumpForNonMainThreads(nsISerialEventTarget* aEventTarget)
      : MessagePump(aEventTarget) {}

  virtual void Run(base::MessagePump::Delegate* aDelegate) override;

 private:
  ~MessagePumpForNonMainThreads() = default;
};

// Extends the CFRunLoopBase message pump to process xpcom events. Based on
// MessagePumpNSRunLoop.
class MessagePumpForNonMainUIThreads final
    : public base::MessagePumpCFRunLoopBase,
      public nsIThreadObserver {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSITHREADOBSERVER

 public:
  explicit MessagePumpForNonMainUIThreads(nsISerialEventTarget* aEventTarget);

  void DoRun(base::MessagePump::Delegate* aDelegate) override;
  void Quit() override;

  nsISerialEventTarget* GetXPCOMThread() override { return mEventTarget; }

 private:
  ~MessagePumpForNonMainUIThreads();

  nsISerialEventTarget* mEventTarget;

  // A source that doesn't do anything but provide something signalable
  // attached to the run loop.  This source will be signalled when Quit
  // is called, to cause the loop to wake up so that it can stop.
  CFRunLoopSourceRef quit_source_;

  // False after Quit is called.
  bool keep_running_;

  DISALLOW_COPY_AND_ASSIGN(MessagePumpForNonMainUIThreads);
};


} /* namespace ipc */
} /* namespace mozilla */

#endif /* IPC_GLUE_MESSAGEPUMP_H_ */
