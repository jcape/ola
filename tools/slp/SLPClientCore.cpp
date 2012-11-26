/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * SLPClientCore.cpp
 * Implementation of SLPClientCore
 * Copyright (C) 2012 Simon Newton
 */

#include <string>
#include <vector>

#include "ola/BaseTypes.h"
#include "ola/Callback.h"
#include "ola/Logging.h"
#include "ola/network/NetworkUtils.h"
#include "tools/slp/SLPClientCore.h"
#include "tools/slp/SLP.pb.h"

namespace ola {
namespace slp {

using ola::slp::proto::SLPService_Stub;
using std::string;
using std::vector;

SLPClientCore::SLPClientCore(ConnectedDescriptor *descriptor)
    : m_descriptor(descriptor),
      m_channel(NULL),
      m_stub(NULL),
      m_connected(false) {
}


SLPClientCore::~SLPClientCore() {
  if (m_connected)
    Stop();
}


/*
 * Setup this client
 * @return true on success, false on failure
 */
bool SLPClientCore::Setup() {
  if (m_connected)
    return false;

  m_channel = new StreamRpcChannel(NULL, m_descriptor);

  if (!m_channel)
    return false;
  m_stub = new SLPService_Stub(m_channel);

  if (!m_stub) {
    delete m_channel;
    return false;
  }
  m_connected = true;
  return true;
}


/*
 * Close the ola connection.
 * @return true on success, false on failure
 */
bool SLPClientCore::Stop() {
  if (m_connected) {
    m_descriptor->Close();
    delete m_channel;
    delete m_stub;
  }
  m_connected = false;
  return 0;
}


/**
 * Register a service in SLP
 * @return true on success, false on failure.
 */
bool SLPClientCore::RegisterService(
    const vector<string> &scopes,
    const string &service,
    uint16_t lifetime,
    SingleUseCallback2<void, const string&, uint16_t> *callback) {
  return GenericRegisterService(scopes, service, lifetime, callback, false);
}


/**
 * Register a service that persists beyond the lifetime of this client.
 * @return true on success, false on failure.
 */
bool SLPClientCore::RegisterPersistentService(
    const vector<string> &scopes,
    const string &service,
    uint16_t lifetime,
    SingleUseCallback2<void, const string&, uint16_t> *callback) {
  return GenericRegisterService(scopes, service, lifetime, callback, true);
}


/**
 * DeRegister a service
 */
bool SLPClientCore::DeRegisterService(
    const vector<string> &scopes,
    const string &service,
    SingleUseCallback2<void, const string&, uint16_t> *callback) {
  if (!m_connected) {
    delete callback;
    return false;
  }

  SimpleRpcController *controller = new SimpleRpcController();
  ola::slp::proto::ServiceDeRegistration request;
  ola::slp::proto::ServiceAck *reply = new ola::slp::proto::ServiceAck();

  request.set_url(service);
  for (vector<string>::const_iterator iter = scopes.begin();
       iter != scopes.end(); ++iter)
    request.add_scope(*iter);

  google::protobuf::Closure *cb = google::protobuf::NewCallback(
      this,
      &SLPClientCore::HandleRegistration,
      NewArgs<register_arg>(controller, reply, callback));
  m_stub->DeRegisterService(controller, &request, reply, cb);
  return true;
}


/**
 * Locate a service in SLP.
 */
bool SLPClientCore::FindService(
    const vector<string> &scopes,
    const string &service_type,
    SingleUseCallback2<void, const string&,
                       const vector<SLPService> &> *callback) {
  if (!m_connected) {
    delete callback;
    return false;
  }

  SimpleRpcController *controller = new SimpleRpcController();
  ola::slp::proto::ServiceRequest request;
  ola::slp::proto::ServiceReply *reply = new ola::slp::proto::ServiceReply();

  request.set_service_type(service_type);
  for (vector<string>::const_iterator iter = scopes.begin();
       iter != scopes.end(); ++iter)
    request.add_scope(*iter);

  google::protobuf::Closure *cb = google::protobuf::NewCallback(
      this,
      &SLPClientCore::HandleFindRequest,
      NewArgs<find_arg>(controller, reply, callback));
  m_stub->FindService(controller, &request, reply, cb);
  return true;
}


// The following are RPC callbacks

/*
 * Called once RegisterService or DeRegisterService completes.
 */
void SLPClientCore::HandleRegistration(register_arg *args) {
  string error_string = "";
  uint16_t response_code;

  if (!args->callback) {
    FreeArgs(args);
    return;
  }

  if (args->controller->Failed()) {
    error_string = args->controller->ErrorText();
  } else {
    response_code = args->reply->error_code();
  }
  args->callback->Run(error_string, response_code);
  FreeArgs(args);
}


/*
 * Called once FindService completes.
 */
void SLPClientCore::HandleFindRequest(find_arg *args) {
  string error_string = "";
  vector<SLPService> services;

  if (!args->callback) {
    FreeArgs(args);
    return;
  }

  if (args->controller->Failed()) {
    error_string = args->controller->ErrorText();
  } else {
    for (int i = 0; i < args->reply->url_entry_size(); ++i) {
      const ola::slp::proto::URLEntry &url_entry = args->reply->url_entry(i);
      SLPService slp_service(url_entry.url(), url_entry.lifetime());
      services.push_back(slp_service);
    }
  }
  args->callback->Run(error_string, services);
  FreeArgs(args);
}


/*
 * Internal method to register services.
 * @return true on success, false on failure
 */
bool SLPClientCore::GenericRegisterService(
    const vector<string> &scopes,
    const string &service,
    uint16_t lifetime,
    SingleUseCallback2<void, const string&, uint16_t> *callback,
    bool persistent) {
  if (!m_connected) {
    delete callback;
    return false;
  }

  SimpleRpcController *controller = new SimpleRpcController();
  ola::slp::proto::ServiceRegistration request;
  ola::slp::proto::ServiceAck *reply = new ola::slp::proto::ServiceAck();

  request.set_url(service);
  for (vector<string>::const_iterator iter = scopes.begin();
       iter != scopes.end(); ++iter)
    request.add_scope(*iter);
  request.set_lifetime(lifetime);
  request.set_persistent(persistent);

  google::protobuf::Closure *cb = google::protobuf::NewCallback(
      this,
      &SLPClientCore::HandleRegistration,
      NewArgs<register_arg>(controller, reply, callback));
  m_stub->RegisterService(controller, &request, reply, cb);
  return true;
}
}  // slp
}  // ola
