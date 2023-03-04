#if defined(TEMPEST_BUILD_VULKAN)

#include "vmeshlethelper.h"

#include "builtin_shader.h"

#include "vdevice.h"
#include "vcommandbuffer.h"
#include "vbuffer.h"
#include "vshader.h"
#include "vpipelinelay.h"
#include "vpipeline.h"

using namespace Tempest::Detail;

VMeshletHelper::VMeshletHelper(VDevice& dev) : dev(dev) {
  const auto ind = MemUsage::StorageBuffer | MemUsage::Indirect    | MemUsage::TransferDst | MemUsage::TransferSrc;
  const auto ms  = MemUsage::StorageBuffer | MemUsage::Indirect    | MemUsage::TransferDst | MemUsage::TransferSrc;
  const auto geo = MemUsage::StorageBuffer | MemUsage::IndexBuffer | MemUsage::TransferDst | MemUsage::TransferSrc;

  indirectSrc = dev.allocator.alloc(nullptr, IndirectScratchSize, 1,1, ind, BufferHeap::Device);
  indirect    = dev.allocator.alloc(nullptr, IndirectMemorySize,  1,1, ind, BufferHeap::Device);

  meshlets    = dev.allocator.alloc(nullptr, MeshletsMemorySize,  1,1, ms,  BufferHeap::Device);

  // scratch     = dev.allocator.alloc(nullptr, PipelineMemorySize,  1,1, geo, BufferHeap::Device);
  scratch     = dev.allocator.alloc(nullptr, PipelineMemorySize,  1,1, geo, BufferHeap::Upload);

  {
    auto c = dev.dataMgr().get();
    auto& cmd = reinterpret_cast<VMeshCommandBuffer&>(*c.get());
    cmd.begin();
    // drawcall-related parts should be set to zeros.
    vkCmdFillBuffer(cmd.impl, indirectSrc.impl, 0, VK_WHOLE_SIZE, 0);
    // meshlet counters
    vkCmdFillBuffer(cmd.impl, meshlets.impl, 0, sizeof(uint32_t)*2, 0);
    // heap counters
    vkCmdFillBuffer(cmd.impl, scratch.impl,  0, sizeof(uint32_t)*2, 0);

    barrier(cmd.impl,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    cmd.end();
    dev.dataMgr().submit(std::move(c));
  }

  try {
    initShaders(dev);

    engLay   = initLayout(dev);
    engPool  = initPool(dev,3);
    engSet   = initDescriptors(dev,engPool,engLay);
    initEngSet(engSet,3);

    compPool = initPool(dev,4);
    compSet  = initDescriptors(dev,compPool,compactageLay.handler->impl);
    initEngSet(compSet,4);

    drawLay  = initDrawLayout(dev);
    drawPool = initPool(dev,1);
    drawSet  = initDescriptors(dev,drawPool,drawLay);
    initDrawSet(drawSet);
    }
  catch(...) {
    cleanup();
    }
  }

VMeshletHelper::~VMeshletHelper() {
  cleanup();
  }

void VMeshletHelper::cleanup() {
  if(compSet!=VK_NULL_HANDLE)
    vkFreeDescriptorSets(dev.device.impl, compPool, 1, &compSet);
  if(compPool!=VK_NULL_HANDLE)
    vkDestroyDescriptorPool(dev.device.impl, compPool, nullptr);

  if(engLay!=VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(dev.device.impl, engLay, nullptr);
  if(engSet!=VK_NULL_HANDLE)
    vkFreeDescriptorSets(dev.device.impl, engPool, 1, &engSet);
  if(engPool!=VK_NULL_HANDLE)
    vkDestroyDescriptorPool(dev.device.impl, engPool, nullptr);

  if(drawLay!=VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(dev.device.impl, drawLay, nullptr);
  if(drawSet!=VK_NULL_HANDLE)
    vkFreeDescriptorSets(dev.device.impl, drawPool, 1, &drawSet);
  if(drawPool!=VK_NULL_HANDLE)
    vkDestroyDescriptorPool(dev.device.impl, drawPool, nullptr);
  }

void VMeshletHelper::bindCS(VkCommandBuffer impl, VkPipelineLayout lay) {
  vkCmdBindDescriptorSets(impl, VK_PIPELINE_BIND_POINT_COMPUTE,
                          lay, 1,
                          1,&engSet,
                          0,nullptr);
  }

void VMeshletHelper::bindVS(VkCommandBuffer impl, VkPipelineLayout lay) {
  vkCmdBindIndexBuffer   (impl, scratch.impl, 1*sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
  vkCmdBindDescriptorSets(impl, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          lay, 1,
                          1,&drawSet,
                          0,nullptr);
  }

void VMeshletHelper::drawIndirect(VkCommandBuffer impl, uint32_t id) {
  assert(id<IndirectCmdCount);
  uint32_t off = id*sizeof(VkDrawIndexedIndirectCommand);
  vkCmdDrawIndexedIndirect(impl, indirect.impl, off, 1, sizeof(VkDrawIndexedIndirectCommand));
  // vkCmdDrawIndexed(impl, 3, 1, 0, 0, 0);
  }

void VMeshletHelper::initRP(VkCommandBuffer impl) {
  if(false) {
    VkDrawIndexedIndirectCommand cmd[3] = {};
    indirect.read(&cmd,0,sizeof(cmd));

    IVec3 cmdSz = {};
    meshlets.read(&cmdSz,2*4,sizeof(cmdSz));

    IVec3 desc[3] = {};
    meshlets.read(&desc,5*4,sizeof(desc));

    uint32_t indSize   = (desc[0].z       ) & 0x3FF;

    //uint32_t ibo[3*3] = {};
    //compacted.read(ibo,0,sizeof(ibo));

    // float    vbo[11*3] = {};
    // uint32_t vboI[11*3] = {};
    // compacted.read(vbo, 3*4,sizeof(vbo));
    // compacted.read(vboI,3*4,sizeof(vboI));

    float sc[12*3+3] = {};
    scratch.read(sc,0,sizeof(sc));

    Log::i("");

    uint32_t zero = 0;
    scratch.update(&zero, 0, 1, 4, 4);
    }
  }

void VMeshletHelper::sortPass(VkCommandBuffer impl, uint32_t meshCallsCount) {
  if(meshCallsCount==0)
    return;

  // prefix summ pass
  barrier(impl, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  vkCmdBindPipeline(impl,VK_PIPELINE_BIND_POINT_COMPUTE,prefixSum.handler->impl);
  vkCmdBindDescriptorSets(impl,VK_PIPELINE_BIND_POINT_COMPUTE, prefixSum.handler->pipelineLayout,
                          0, 1,&compSet, 0,nullptr);
  vkCmdPushConstants(impl,prefixSum.handler->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&meshCallsCount);
  vkCmdDispatch(impl, 1,1,1); // one threadgroup for prefix pass

  // compactage pass
  barrier(impl,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
  vkCmdBindPipeline(impl,VK_PIPELINE_BIND_POINT_COMPUTE,compactage.handler->impl);
  vkCmdBindDescriptorSets(impl,VK_PIPELINE_BIND_POINT_COMPUTE, compactage.handler->pipelineLayout,
                          0, 1,&compSet, 0,nullptr);
  vkCmdDispatch(impl, 1024,1,1); // persistent(almost) threads

  // ready for draw
  barrier(impl,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
  }

void VMeshletHelper::firstDraw(VkCommandBuffer impl) {
  // wait for draw
  barrier(impl,
          VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  }

void VMeshletHelper::barrier(VkCommandBuffer      impl,
                             VkPipelineStageFlags srcStageMask,
                             VkPipelineStageFlags dstStageMask,
                             VkAccessFlags        srcAccessMask,
                             VkAccessFlags        dstAccessMask) {
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;

  vkCmdPipelineBarrier(impl,
                       srcStageMask,
                       dstStageMask,
                       0,
                       1, &barrier,
                       0, nullptr,
                       0, nullptr);
  }

VkDescriptorSetLayout VMeshletHelper::initLayout(VDevice& device) {
  VkDescriptorSetLayoutBinding bind[3] = {};

  for(int i=0; i<3; ++i) {
    bind[i].binding         = i;
    bind[i].descriptorCount = 1;
    bind[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

  VkDescriptorSetLayoutCreateInfo info={};
  info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.pNext        = nullptr;
  info.flags        = 0;
  info.bindingCount = 3;
  info.pBindings    = bind;

  VkDescriptorSetLayout ret = VK_NULL_HANDLE;
  vkAssert(vkCreateDescriptorSetLayout(dev.device.impl,&info,nullptr,&ret));
  return ret;
  }

VkDescriptorSetLayout VMeshletHelper::initDrawLayout(VDevice& device) {
  VkDescriptorSetLayoutBinding bind[1] = {};
  bind[0].binding         = 0;
  bind[0].descriptorCount = 1;
  bind[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bind[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo info={};
  info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.pNext        = nullptr;
  info.flags        = 0;
  info.bindingCount = 1;
  info.pBindings    = bind;

  VkDescriptorSetLayout ret = VK_NULL_HANDLE;
  vkAssert(vkCreateDescriptorSetLayout(dev.device.impl,&info,nullptr,&ret));
  return ret;
  }

VkDescriptorPool VMeshletHelper::initPool(VDevice& device, uint32_t cnt) {
  VkDescriptorPoolSize poolSize[1] = {};
  poolSize[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSize[0].descriptorCount = cnt;

  VkDescriptorPoolCreateInfo poolInfo = {};
  poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets       = 1;
  poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.poolSizeCount = uint32_t(1);
  poolInfo.pPoolSizes    = poolSize;

  VkDevice dev = device.device.impl;
  VkDescriptorPool ret = VK_NULL_HANDLE;
  vkAssert(vkCreateDescriptorPool(dev,&poolInfo,nullptr,&ret));
  return ret;
  }

VkDescriptorSet VMeshletHelper::initDescriptors(VDevice& device,
                                                VkDescriptorPool pool,
                                                VkDescriptorSetLayout lay) {
  VkDescriptorSetAllocateInfo allocInfo = {};
  allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool     = pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts        = &lay;

  VkDevice        dev  = device.device.impl;
  VkDescriptorSet desc = VK_NULL_HANDLE;
  vkAssert(vkAllocateDescriptorSets(dev,&allocInfo,&desc));
  return desc;
  }

void VMeshletHelper::initEngSet(VkDescriptorSet set, uint32_t cnt) {
  VkDescriptorBufferInfo buf[4] = {};
  buf[0].buffer = indirectSrc.impl;
  buf[0].offset = 0;
  buf[0].range  = VK_WHOLE_SIZE;

  buf[1].buffer = meshlets.impl;
  buf[1].offset = 0;
  buf[1].range  = VK_WHOLE_SIZE;

  buf[2].buffer = scratch.impl;
  buf[2].offset = 0;
  buf[2].range  = VK_WHOLE_SIZE;

  buf[3].buffer = indirect.impl;
  buf[3].offset = 0;
  buf[3].range  = VK_WHOLE_SIZE;

  VkWriteDescriptorSet write[4] = {};
  for(uint32_t i=0; i<cnt; ++i) {
    write[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write[i].dstSet          = set;
    write[i].dstBinding      = uint32_t(i);
    write[i].dstArrayElement = 0;
    write[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write[i].descriptorCount = 1;
    write[i].pBufferInfo     = &buf[i];
    }

  vkUpdateDescriptorSets(dev.device.impl, cnt, write, 0, nullptr);
  }

void VMeshletHelper::initDrawSet(VkDescriptorSet set) {
  VkDescriptorBufferInfo buf[1] = {};
  buf[0].buffer = scratch.impl;
  buf[0].offset = 0;
  buf[0].range  = VK_WHOLE_SIZE;

  VkWriteDescriptorSet write = {};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet          = set;
  write.dstBinding      = 0;
  write.dstArrayElement = 0;
  write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.descriptorCount = 1;
  write.pBufferInfo     = &buf[0];
  vkUpdateDescriptorSets(dev.device.impl, 1, &write, 0, nullptr);
  }

void VMeshletHelper::initShaders(VDevice& device) {
  auto prefixSumCs = DSharedPtr<VShader*>(new VShader(device,mesh_prefix_pass_comp_sprv,sizeof(mesh_prefix_pass_comp_sprv)));
  prefixSumLay  = DSharedPtr<VPipelineLay*> (new VPipelineLay (device,&prefixSumCs.handler->lay));
  prefixSum     = DSharedPtr<VCompPipeline*>(new VCompPipeline(device,*prefixSumLay.handler,*prefixSumCs.handler));

  auto compactageCs = DSharedPtr<VShader*>(new VShader(device,mesh_compactage_comp_sprv,sizeof(mesh_compactage_comp_sprv)));
  compactageLay = DSharedPtr<VPipelineLay*> (new VPipelineLay (device,&compactageCs.handler->lay));
  compactage    = DSharedPtr<VCompPipeline*>(new VCompPipeline(device,*compactageLay.handler,*compactageCs.handler));
  }

#endif
