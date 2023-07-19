// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <mscclpp/semaphore.hpp>

#include "api.h"
#include "debug.h"

namespace mscclpp {

static NonblockingFuture<RegisteredMemory> setupInboundSemaphoreId(Communicator& communicator, Connection* connection,
                                                                   void* localInboundSemaphoreId) {
  auto localInboundSemaphoreIdsRegMem =
      communicator.registerMemory(localInboundSemaphoreId, sizeof(uint64_t), connection->transport());
  communicator.sendMemoryOnSetup(localInboundSemaphoreIdsRegMem, connection->remoteRank(), connection->tag());
  return communicator.recvMemoryOnSetup(connection->remoteRank(), connection->tag());
}

MSCCLPP_API_CPP Host2DeviceSemaphore::Host2DeviceSemaphore(Communicator& communicator,
                                                           std::shared_ptr<Connection> connection)
    : BaseSemaphore(allocUniqueCuda<uint64_t>(), allocUniqueCuda<uint64_t>(), std::make_unique<uint64_t>()),
      connection_(connection) {
  remoteInboundSemaphoreIdsRegMem_ =
      setupInboundSemaphoreId(communicator, connection.get(), localInboundSemaphore_.get());
}

MSCCLPP_API_CPP std::shared_ptr<Connection> Host2DeviceSemaphore::connection() { return connection_; }

MSCCLPP_API_CPP void Host2DeviceSemaphore::signal() {
  connection_->updateAndSync(remoteInboundSemaphoreIdsRegMem_.get(), 0, outboundSemaphore_.get(),
                             *outboundSemaphore_ + 1);
}

MSCCLPP_API_CPP Host2DeviceSemaphore::DeviceHandle Host2DeviceSemaphore::deviceHandle() {
  Host2DeviceSemaphore::DeviceHandle device;
  device.inboundSemaphoreId = localInboundSemaphore_.get();
  device.expectedInboundSemaphoreId = expectedInboundSemaphore_.get();
  return device;
}

MSCCLPP_API_CPP Host2HostSemaphore::Host2HostSemaphore(Communicator& communicator,
                                                       std::shared_ptr<Connection> connection)
    : BaseSemaphore(std::make_unique<uint64_t>(), std::make_unique<uint64_t>(), std::make_unique<uint64_t>()),
      connection_(connection) {
  if (connection->transport() == Transport::CudaIpc) {
    throw Error("Host2HostSemaphore cannot be used with CudaIpc transport", ErrorCode::InvalidUsage);
  }
  remoteInboundSemaphoreIdsRegMem_ =
      setupInboundSemaphoreId(communicator, connection.get(), localInboundSemaphore_.get());
}

MSCCLPP_API_CPP std::shared_ptr<Connection> Host2HostSemaphore::connection() { return connection_; }

MSCCLPP_API_CPP void Host2HostSemaphore::signal() {
  connection_->updateAndSync(remoteInboundSemaphoreIdsRegMem_.get(), 0, outboundSemaphore_.get(),
                             *outboundSemaphore_ + 1);
}

MSCCLPP_API_CPP void Host2HostSemaphore::wait() {
  (*expectedInboundSemaphore_) += 1;
  while (*(volatile uint64_t*)localInboundSemaphore_.get() < (*expectedInboundSemaphore_)) {
  }
}

MSCCLPP_API_CPP SmDevice2DeviceSemaphore::SmDevice2DeviceSemaphore(Communicator& communicator,
                                                                   std::shared_ptr<Connection> connection)
    : BaseSemaphore(allocUniqueCuda<uint64_t>(), allocUniqueCuda<uint64_t>(), allocUniqueCuda<uint64_t>()) {
  if (connection->transport() == Transport::CudaIpc) {
    remoteInboundSemaphoreIdsRegMem_ =
        setupInboundSemaphoreId(communicator, connection.get(), localInboundSemaphore_.get());
    INFO(MSCCLPP_INIT, "Creating a direct semaphore for CudaIPC transport from %d to %d",
         communicator.bootstrap()->getRank(), connection->remoteRank());
    isRemoteInboundSemaphoreIdSet_ = true;
  } else if (AllIBTransports.has(connection->transport())) {
    // We don't need to really with any of the IB transports, since the values will be local
    INFO(MSCCLPP_INIT, "Creating a direct semaphore for IB transport from %d to %d",
         communicator.bootstrap()->getRank(), connection->remoteRank());
    isRemoteInboundSemaphoreIdSet_ = false;
  }
}

MSCCLPP_API_CPP SmDevice2DeviceSemaphore::DeviceHandle SmDevice2DeviceSemaphore::deviceHandle() const {
  SmDevice2DeviceSemaphore::DeviceHandle device;
  device.remoteInboundSemaphoreId = isRemoteInboundSemaphoreIdSet_
                                        ? reinterpret_cast<uint64_t*>(remoteInboundSemaphoreIdsRegMem_.get().data())
                                        : nullptr;
  device.inboundSemaphoreId = localInboundSemaphore_.get();
  device.expectedInboundSemaphoreId = expectedInboundSemaphore_.get();
  device.outboundSemaphoreId = outboundSemaphore_.get();
  return device;
};

}  // namespace mscclpp