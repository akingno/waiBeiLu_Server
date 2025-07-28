//
// Created by jacob on 2025/7/28.
//
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include "nlohmann/json.hpp"
#include "fstream"
#include "chat_participant.h"
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

using boost::asio::ip::tcp;
using json = nlohmann::json;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;
typedef std::shared_ptr<chat_participant> chat_participant_ptr;



class chat_room {
 public:
  void join(chat_participant_ptr participant) {
    participants_.insert(participant);
    for (auto msg: recent_msgs_)
      participant->deliver(msg);
  }

  void leave(chat_participant_ptr participant) {
    participants_.erase(participant);
  }

  void deliver(const chat_message& msg) {
    recent_msgs_.push_back(msg);
    while (recent_msgs_.size() > max_recent_msgs)
      recent_msgs_.pop_front();

    for (auto participant: participants_)
      participant->deliver(msg);
  }

 private:
  std::set<chat_participant_ptr> participants_;
  enum { max_recent_msgs = 100 };
  chat_message_queue recent_msgs_;
};

//----------------------------------------------------------------------

class chat_session
    : public chat_participant,
      public std::enable_shared_from_this<chat_session>
{
 public:
  chat_session(tcp::socket socket, chat_room& room) : socket_(std::move(socket)), room_(room) {}

  void start() {
    // 不在此加入, 等验证过后再加入
    //room_.join(shared_from_this());
    do_read_header();
  }

  void deliver(const chat_message& msg) {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress) {
      do_write();
    }
  }

 private:
  void do_read_header() {
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
                            boost::asio::buffer(read_msg_.data(), chat_message::header_length),
                            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                              if (!ec && read_msg_.decode_header()) {
                                do_read_body();
                              }
                              else {
                                if (in_room_) {
                                  room_.leave(shared_from_this());
                                }
                                socket_.close();
                              }
                            });
  }

  void do_read_body() {
    auto self(shared_from_this());
    boost::asio::async_read(socket_,
                            boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
                            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                              if (!ec) {

                                std::string data(read_msg_.body(), read_msg_.body_length());
                                json recv_json = json::parse(data);
                                std::string type = recv_json.value("type", "");

                                if (type == "login") {

                                  // 提取用户名和密码
                                  std::string user = recv_json.value("username", "");
                                  std::string pwd  = recv_json.value("pwd", "");
                                  bool success = false;
                                  // 在用户文件中检验用户名/密码
                                  spdlog::info("login by: {}, {}", user, pwd);
                                  std::ifstream file("users.txt");
                                  if (file.is_open()) {
                                    std::string file_user, file_pwd;
                                    while (file >> file_user >> file_pwd) {
                                      if (file_user == user && file_pwd == pwd) {
                                        success = true;
                                        username_ = user;    // 保存用户名
                                        logged_in_ = true;   // 标记为已登录
                                        break;
                                      }
                                    }
                                    file.close();
                                  }

                                  // 构造登录结果 JSON 并发送给客户端本身
                                  json resp;
                                  resp["type"] = "login";
                                  resp["status"] = success ? "allow" : "deny";
                                  spdlog::info("login result: {}", success);
                                  std::string resp_str = resp.dump();
                                  chat_message resp_msg;
                                  resp_msg.body_length(resp_str.size());
                                  std::memcpy(resp_msg.body(), resp_str.c_str(), resp_msg.body_length());
                                  resp_msg.encode_header();
                                  deliver(resp_msg);  // 发送响应给当前客户端

                                  // 如果验证失败，这里可以选择断开连接，或者等待客户端再次发送登录请求
                                  // 本示例中不立即断开，让客户端有机会重试
                                }
                                else if (type == "enter_room") {
                                  spdlog::info("enter room");
                                  // 登录成功的用户发送进入聊天室请求后，正式将其加入房间
                                  if (logged_in_ && !in_room_) {
                                    in_room_ = true;
                                    room_.join(shared_from_this());  // 将该会话加入聊天室
                                                                   // （加入后，chat_room::join 会自动把最近的消息历史发送给该用户）
                                  }
                                  // enter_room 消息不转发给其他客户端
                                }
                                else if (type == "chat") {
                                  // 仅转发已登录且进入聊天室用户的聊天消息
                                  if (logged_in_ && in_room_) {
                                    recv_json["username"] = username_;
                                    // 包含类型、用户名和内容
                                    std::string out_str = recv_json.dump();
                                    chat_message chat_msg;
                                    chat_msg.body_length(out_str.size());
                                    std::memcpy(chat_msg.body(), out_str.c_str(), chat_msg.body_length());
                                    chat_msg.encode_header();
                                    room_.deliver(chat_msg);  // 广播给聊天室内所有用户

                                    spdlog::info("chat: {}, {}", username_, out_str);
                                  }
                                  // 未登录或不在房间的用户消息不处理
                                }

                                // 继续读取下一条消息
                                do_read_header();
                              }
                              else {
                                if (in_room_) {
                                  room_.leave(shared_from_this());
                                }
                                socket_.close();
                              }
                            });
  }

  void do_write() {
    auto self(shared_from_this());
    boost::asio::async_write(socket_,
                             boost::asio::buffer(write_msgs_.front().data(),
                                                 write_msgs_.front().length()),
                             [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                               if (!ec) {
                                 write_msgs_.pop_front();
                                 if (!write_msgs_.empty()) {
                                   do_write();
                                 }
                               }
                               else {
                                 if (in_room_) {
                                   room_.leave(shared_from_this());
                                 }
                                 socket_.close();
                               }
                             });
  }

  tcp::socket socket_;
  chat_room& room_;
  chat_message read_msg_;
  chat_message_queue write_msgs_;

  std::string username_;
  bool logged_in_       = false;
  bool in_room_         = false;
};

//----------------------------------------------------------------------

class chat_server {
 public:
  chat_server(boost::asio::io_context& io_context, const tcp::endpoint& endpoint) : acceptor_(io_context, endpoint) {
    do_accept();
  }

 private:
  void do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
          if (!ec) {
            std::make_shared<chat_session>(std::move(socket), room_)->start();
          }
          do_accept();
        });
  }

  tcp::acceptor acceptor_;
  chat_room room_;
};

//----------------------------------------------------------------------

int main(int argc, char* argv[]) {
  try
  {
    if (argc < 2) {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }

    boost::asio::io_context io_context;

    std::list<chat_server> servers;
    for (int i = 1; i < argc; ++i) {
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
      servers.emplace_back(io_context, endpoint);
    }

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}