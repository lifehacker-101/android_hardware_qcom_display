/*
* Copyright (c) 2011, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef OVERLAY_MEM_H
#define OVERLAY_MEM_H

#include <sys/mman.h>
#include <fcntl.h>
#include <alloc_controller.h>
#include <memalloc.h>

#include "gralloc_priv.h"
#include "overlayUtils.h"
#include "mdpWrapper.h"

#define SIZE_1M 0x00100000

namespace overlay {

/*
* Holds base address, offset and the fd
* */
class OvMem {
public:
    /* ctor init*/
    explicit OvMem();

    /* dtor DO NOT call close so it can be copied */
    ~OvMem();

    /* Use libgralloc to retrieve fd, base addr, alloc type */
    bool open(uint32_t numbufs,
            uint32_t bufSz, bool isSecure);

    /* close fd. assign base address to invalid*/
    bool close();

    /* return underlying fd */
    int getFD() const;

    /* return true if fd is valid and base address is valid */
    bool valid() const;

    /* dump the state of the object */
    void dump() const;

    /* return underlying address */
    void* addr() const;

    /* return underlying offset */
    uint32_t bufSz() const;

    /* return number of bufs */
    uint32_t numBufs() const ;

    /* Set / unset secure with MDP */
    bool setSecure(bool enable);

private:
    /* actual os fd */
    int mFd;

    /* points to base addr (mmap)*/
    void* mBaseAddr;

    /* allocated buffer type determined by gralloc (ashmem, ion, etc) */
    int mAllocType;

    /* holds buf size sent down by the client */
    uint32_t mBufSz;

    /* num of bufs */
    uint32_t mNumBuffers;

    /* gralloc alloc controller */
    gralloc::IAllocController* mAlloc;

    /*Holds the aligned buffer size used for actual allocation*/
    uint32_t mBufSzAligned;

    /* Flags if the buffer has been secured by MDP */
    bool mSecured;
};

//-------------------Inlines-----------------------------------

using gralloc::IMemAlloc;
using gralloc::alloc_data;

inline OvMem::OvMem() {
    mFd = -1;
    mBaseAddr = MAP_FAILED;
    mAllocType = 0;
    mBufSz = 0;
    mNumBuffers = 0;
    mSecured = false;
    mAlloc = gralloc::IAllocController::getInstance();
}

inline OvMem::~OvMem() { }

inline bool OvMem::open(uint32_t numbufs,
        uint32_t bufSz, bool isSecure)
{
    alloc_data data;
    int allocFlags = GRALLOC_USAGE_PRIVATE_IOMMU_HEAP;
    int err = 0;
    OVASSERT(numbufs && bufSz, "numbufs=%d bufSz=%d", numbufs, bufSz);
    mBufSz = bufSz;

    if(isSecure) {
        allocFlags = GRALLOC_USAGE_PRIVATE_MM_HEAP;
        allocFlags |= GRALLOC_USAGE_PROTECTED;
        mBufSzAligned = utils::align(bufSz, SIZE_1M);
        data.align = SIZE_1M;
    } else {
        mBufSzAligned = bufSz;
        data.align = getpagesize();
    }

    // Allocate uncached rotator buffers
    allocFlags |= GRALLOC_USAGE_PRIVATE_UNCACHED;

    mNumBuffers = numbufs;

    data.base = 0;
    data.fd = -1;
    data.offset = 0;
    data.size = mBufSzAligned * mNumBuffers;
    data.uncached = true;

    err = mAlloc->allocate(data, allocFlags);
    if (err != 0) {
        ALOGE("OvMem: Error allocating memory");
        return false;
    }

    mFd = data.fd;
    mBaseAddr = data.base;
    mAllocType = data.allocType;

    if(isSecure) {
        setSecure(true);
    }

    return true;
}

inline bool OvMem::close()
{
    int ret = 0;

    if(!valid()) {
        return true;
    }

    if(mSecured) {
        setSecure(false);
    }

    IMemAlloc* memalloc = mAlloc->getAllocator(mAllocType);
    ret = memalloc->free_buffer(mBaseAddr, mBufSzAligned * mNumBuffers, 0, mFd);
    if (ret != 0) {
        ALOGE("OvMem: error freeing buffer");
        return false;
    }

    mFd = -1;
    mBaseAddr = MAP_FAILED;
    mAllocType = 0;
    mBufSz = 0;
    mBufSzAligned = 0;
    mNumBuffers = 0;
    return true;
}

inline bool OvMem::setSecure(bool enable) {
    OvFD fbFd;
    if(!utils::openDev(fbFd, 0, Res::fbPath, O_RDWR)) {
        ALOGE("OvMem::%s failed to init fb0", __FUNCTION__);
        return false;
    }
    struct msmfb_secure_config config;
    utils::memset0(config);
    config.fd = mFd;
    config.enable = enable;
    if(!mdp_wrapper::setSecureBuffer(fbFd.getFD(), config)) {
        ALOGE("OvMem::%s failed enable=%d", __FUNCTION__, enable);
        fbFd.close();
        mSecured = false;
        return false;
    }
    fbFd.close();
    mSecured = enable;
    return true;
}

inline bool OvMem::valid() const
{
    return (mFd != -1) && (mBaseAddr != MAP_FAILED);
}

inline int OvMem::getFD() const
{
    return mFd;
}

inline void* OvMem::addr() const
{
    return mBaseAddr;
}

inline uint32_t OvMem::bufSz() const
{
    return mBufSz;
}

inline uint32_t OvMem::numBufs() const
{
    return mNumBuffers;
}

inline void OvMem::dump() const
{
    ALOGE("== Dump OvMem start ==");
    ALOGE("fd=%d addr=%p type=%d bufsz=%u AlignedBufSz=%u",
           mFd, mBaseAddr, mAllocType, mBufSz, mBufSzAligned);
    ALOGE("== Dump OvMem end ==");
}

} // overlay

#endif // OVERLAY_MEM_H
