#include "ScratchWorkspace.h"

#include <Arduino.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>

namespace {
uint8_t* gBuffer = nullptr;
size_t gCapacity = 0;
bool gLeased = false;
bool gBorrowed = false;
const char* gLeaseOwner = nullptr;
const char* gBorrowOwner = nullptr;
SemaphoreHandle_t gMutex = nullptr;

bool ensureMutex() {
  if (gMutex) return true;
  gMutex = xSemaphoreCreateMutex();
  if (!gMutex) {
    LOG_ERR("SCR", "Failed to create scratch workspace mutex");
    return false;
  }
  return true;
}

bool lock() {
  if (!ensureMutex()) return false;
  return xSemaphoreTake(gMutex, portMAX_DELAY) == pdTRUE;
}

void unlock() { xSemaphoreGive(gMutex); }

}  // namespace

namespace ScratchWorkspace {

void releaseLease(Lease& lease) {
  if (!lease.valid) return;
  if (!lock()) {
    lease.valid = false;
    lease.capacity = 0;
    return;
  }

  if (gBorrowed) {
    LOG_ERR("SCR", "Releasing scratch workspace while borrowed by %s", gBorrowOwner ? gBorrowOwner : "unknown");
    gBorrowed = false;
    gBorrowOwner = nullptr;
  }
  free(gBuffer);
  gBuffer = nullptr;
  gCapacity = 0;
  gLeased = false;
  gLeaseOwner = nullptr;
  lease.valid = false;
  lease.capacity = 0;
  unlock();
}

void releaseBorrow(Borrow& borrow) {
  if (!borrow.buffer) return;
  if (lock()) {
    gBorrowed = false;
    gBorrowOwner = nullptr;
    unlock();
  }
  borrow.buffer = nullptr;
  borrow.capacity = 0;
}

Lease::Lease(Lease&& other) noexcept : valid(other.valid), capacity(other.capacity) {
  other.valid = false;
  other.capacity = 0;
}

Lease& Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    releaseLease(*this);
    valid = other.valid;
    capacity = other.capacity;
    other.valid = false;
    other.capacity = 0;
  }
  return *this;
}

Lease::~Lease() { releaseLease(*this); }

Borrow::Borrow(Borrow&& other) noexcept : buffer(other.buffer), capacity(other.capacity) {
  other.buffer = nullptr;
  other.capacity = 0;
}

Borrow& Borrow::operator=(Borrow&& other) noexcept {
  if (this != &other) {
    releaseBorrow(*this);
    buffer = other.buffer;
    capacity = other.capacity;
    other.buffer = nullptr;
    other.capacity = 0;
  }
  return *this;
}

Borrow::~Borrow() { releaseBorrow(*this); }

Lease acquire(const size_t bytes, const char* owner) {
  Lease lease;
  if (bytes == 0 || !lock()) return lease;
  if (gLeased) {
    LOG_DBG("SCR", "Scratch workspace already leased by %s", gLeaseOwner ? gLeaseOwner : "unknown");
    unlock();
    return lease;
  }

  gBuffer = static_cast<uint8_t*>(malloc(bytes));
  if (!gBuffer) {
    LOG_ERR("SCR", "Failed to allocate scratch workspace for %s (%u bytes, free=%u, maxAlloc=%u)",
            owner ? owner : "unknown", static_cast<unsigned>(bytes), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    unlock();
    return lease;
  }

  gCapacity = bytes;
  gLeased = true;
  gBorrowed = false;
  gLeaseOwner = owner;
  lease.valid = true;
  lease.capacity = bytes;
  unlock();
  return lease;
}

Borrow borrow(const size_t minBytes, const char* owner) {
  Borrow borrowed;
  if (minBytes == 0 || !lock()) return borrowed;
  if (!gLeased || gBorrowed || gCapacity < minBytes) {
    unlock();
    return borrowed;
  }

  gBorrowed = true;
  gBorrowOwner = owner;
  borrowed.buffer = gBuffer;
  borrowed.capacity = gCapacity;
  unlock();
  return borrowed;
}

}  // namespace ScratchWorkspace
