//
// Created by jacob on 2025/7/29.
//

#ifndef WAIBEILUSERVER__CHAT_PARTICIPANT_H_
#define WAIBEILUSERVER__CHAT_PARTICIPANT_H_
#include "chat_message.h"

class chat_participant
{
 public:
  virtual ~chat_participant() {}
  virtual void deliver(const chat_message& msg) = 0;
};


#endif//WAIBEILUSERVER__CHAT_PARTICIPANT_H_
