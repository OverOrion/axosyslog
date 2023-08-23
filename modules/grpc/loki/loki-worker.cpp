/*
 * Copyright (c) 2023 László Várady
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "loki-worker.hpp"
#include "loki-dest.hpp"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"
#include "scratch-buffers.h"
#include "logmsg/type-hinting.h"
#include "compat/cpp-end.h"

#include "push.grpc.pb.h"

#include <string>
#include <sstream>
#include <chrono>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

using syslogng::grpc::loki::DestinationWorker;
using syslogng::grpc::loki::DestinationDriver;
using google::protobuf::FieldDescriptor;

struct _LokiDestWorker
{
  LogThreadedDestWorker super;
  DestinationWorker *cpp;
};

DestinationWorker::DestinationWorker(LokiDestWorker *s) : super(s)
{
}

DestinationWorker::~DestinationWorker()
{
}

bool
DestinationWorker::init()
{
  DestinationDriver *owner = this->get_owner();

  ::grpc::ChannelArguments args{};

  if (owner->keepalive_time != -1)
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, owner->keepalive_time);
  if (owner->keepalive_timeout != -1)
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, owner->keepalive_timeout);
  if (owner->keepalive_max_pings_without_data != -1)
    args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, owner->keepalive_max_pings_without_data);

  args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

  auto credentials = ::grpc::InsecureChannelCredentials();
  if (!credentials)
    {
      msg_error("Error querying Loki credentials", log_pipe_location_tag((LogPipe *) this->super->super.owner));
      return false;
    }

  this->channel = ::grpc::CreateCustomChannel(owner->get_url(), credentials, args);
  if (!this->channel)
    {
      msg_error("Error creating Loki gRPC channel", log_pipe_location_tag((LogPipe *) this->super->super.owner));
      return false;
    }

  this->stub = logproto::Pusher().NewStub(channel);

  return log_threaded_dest_worker_init_method(&this->super->super);
}

void
DestinationWorker::deinit()
{
  log_threaded_dest_worker_deinit_method(&this->super->super);
}

bool
DestinationWorker::connect()
{
  msg_debug("Connecting to Loki", log_pipe_location_tag((LogPipe *) this->super->super.owner));

  std::chrono::system_clock::time_point connect_timeout =
    std::chrono::system_clock::now() + std::chrono::seconds(10);

  if (!this->channel->WaitForConnected(connect_timeout))
    return false;

  this->connected = true;
  return true;
}

void
DestinationWorker::disconnect()
{
  if (!this->connected)
    return;

  this->connected = false;
}

LogThreadedResult
DestinationWorker::insert(LogMessage *msg)
{
  DestinationDriver *owner = this->get_owner();

  msg_trace("Message added to Loki batch", log_pipe_location_tag((LogPipe *) this->super->super.owner));

  return LTR_QUEUED;
}

LogThreadedResult
DestinationWorker::flush(LogThreadedFlushMode mode)
{
  if (this->super->super.batch_size == 0)
    return LTR_SUCCESS;


  msg_debug("Loki batch delivered", log_pipe_location_tag((LogPipe *) this->super->super.owner));
  return LTR_SUCCESS;
}

DestinationDriver *
DestinationWorker::get_owner()
{
  return loki_dd_get_cpp((LokiDestDriver *) this->super->super.owner);
}

/* C Wrappers */

static LogThreadedResult
_insert(LogThreadedDestWorker *s, LogMessage *msg)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  return self->cpp->insert(msg);
}

static LogThreadedResult
_flush(LogThreadedDestWorker *s, LogThreadedFlushMode mode)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  return self->cpp->flush(mode);
}

static gboolean
_connect(LogThreadedDestWorker *s)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  return self->cpp->connect();
}

static void
_disconnect(LogThreadedDestWorker *s)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  self->cpp->disconnect();
}

static gboolean
_init(LogThreadedDestWorker *s)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  return self->cpp->init();
}

static void
_deinit(LogThreadedDestWorker *s)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  self->cpp->deinit();
}

static void
_free(LogThreadedDestWorker *s)
{
  LokiDestWorker *self = (LokiDestWorker *) s;
  delete self->cpp;

  log_threaded_dest_worker_free_method(s);
}

LogThreadedDestWorker *
loki_dw_new(LogThreadedDestDriver *o, gint worker_index)
{
  LokiDestWorker *self = g_new0(LokiDestWorker, 1);

  log_threaded_dest_worker_init_instance(&self->super, o, worker_index);

  self->cpp = new DestinationWorker(self);

  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.connect = _connect;
  self->super.disconnect = _disconnect;
  self->super.insert = _insert;
  self->super.flush = _flush;
  self->super.free_fn = _free;

  return &self->super;
}
