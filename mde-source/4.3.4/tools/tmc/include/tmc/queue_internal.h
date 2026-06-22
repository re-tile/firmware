// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

//! @file
//!
//! Shared definitions for the various TMC queue implementations.

//! @addtogroup tmc_queue
//! @{
#ifndef __TMC_QUEUE_INTERNAL_H__
#define __TMC_QUEUE_INTERNAL_H__

//! Passed to TMC_QUEUE() to indicate no special flags.
#define TMC_QUEUE_NO_FLAGS (0)

//! Passed to TMC_QUEUE() to indicate that there is only a single
//! enqueueing task and the implementation can omit any enqueue locks.
#define TMC_QUEUE_SINGLE_SENDER (1 << 0)

//! Passed to TMC_QUEUE() to indicate that there is only a single
//! enqueueing task and the implementation can omit any dequeue locks.
#define TMC_QUEUE_SINGLE_RECEIVER (1 << 1)

#endif // __TMC_QUEUE_INTERNAL_H__

//! @}
