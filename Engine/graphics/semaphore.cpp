#include "semaphore.h"
#include <Tempest/Device>

using namespace Tempest;

Semaphore::Semaphore(HeadlessDevice &dev, AbstractGraphicsApi::Semaphore *impl)
  :dev(&dev),impl(impl) {
  }

Semaphore::~Semaphore() {
  delete impl.handler;
  }
