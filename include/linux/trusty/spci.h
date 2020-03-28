/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google, Inc.
 */

#pragma once

/*
 * Subset of "Arm Secure Partition Client Interface Specification 1.0" Beta
 * 1_0-4+ (https://developer.arm.com/docs/den0077/a) needed for shared memory.
 *
 * Post -4 spec changes:
 * 1. State Fragment length must be equal to the Total Length if no fragments
 *    are passed.
 * 2. State that TAG value must be passed back as is in RETRIEVE_RESP
 */

#include <linux/bits.h>

#include "smcall.h"

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(e) _Static_assert(e, #e)
#endif

#define SPCI_CURRENT_VERSION_MAJOR (0U)
#define SPCI_CURRENT_VERSION_MINOR (9U)

#define SPCI_VERSION_TO_MAJOR(version) ((version) >> 16)
#define SPCI_VERSION_TO_MINOR(version) ((version) & (0xffff))
#define SPCI_VERSION(major, minor) (((major) << 16) | (minor))
#define SPCI_CURRENT_VERSION \
	SPCI_VERSION(SPCI_CURRENT_VERSION_MAJOR, SPCI_CURRENT_VERSION_MINOR)

#define SMC_ENTITY_SHARED_MEMORY 4

#define SMC_FASTCALL_NR_SHARED_MEMORY(nr) \
	SMC_FASTCALL_NR(SMC_ENTITY_SHARED_MEMORY, nr)
#define SMC_FASTCALL64_NR_SHARED_MEMORY(nr) \
	SMC_FASTCALL64_NR(SMC_ENTITY_SHARED_MEMORY, nr)

/**
 * struct spci_constituent_memory_region_descriptor - contiguous memory region
 * @address:
 *         Start address of contiguous memory region. Must be 4K page aligned.
 * @page_count:
 *         Number of 4K pages in region.
 * @reserved_12_15:
 *         Reserve bytes 12-15 to pad struct size to 16 bytes.
 */
struct spci_constituent_memory_region_descriptor {
	uint64_t address;
	uint32_t page_count;
	uint32_t reserved_12_15;
};
STATIC_ASSERT(sizeof(struct spci_constituent_memory_region_descriptor) == 16);

/**
 * struct spci_composite_memory_region_descriptor - composite memory region
 * @total_page_count:
 *         Number of 4k pages in memory region. Must match sum of
 *         @address_range_array[].page_count.
 * @address_range_count:
 *         Number of entries in @address_range_array.
 * @reserved_8_15:
 *         Reserve bytes 8-15 to pad struct size to 16 byte alignment and
 *         make @address_range_array 16 byte aligned.
 * @address_range_array:
 *         Array of &struct spci_constituent_memory_region_descriptor entries.
 */
struct spci_composite_memory_region_descriptor {
	uint32_t total_page_count;
	uint32_t address_range_count;
	uint64_t reserved_8_15;
	struct spci_constituent_memory_region_descriptor address_range_array[];
};
STATIC_ASSERT(sizeof(struct spci_composite_memory_region_descriptor) == 16);

/**
 * typedef spci_mem_attr8_t - Memory attributes
 *
 * * @SPCI_MEM_ATTR_NORMAL_MEMORY_UNCACHED
 *     Uncached memory type.
 * * @SPCI_MEM_ATTR_NORMAL_MEMORY_CACHED_WB
 *     Cached write-back memory type.
 * * @SPCI_MEM_ATTR_INNER_SHAREABLE
 *     Shareablility.
 */
typedef uint8_t spci_mem_attr8_t;
#define SPCI_MEM_ATTR_DEVICE_NGNRNE (0x4U << 2)
#define SPCI_MEM_ATTR_DEVICE_NGNRE (0x5U << 2)
#define SPCI_MEM_ATTR_DEVICE_NGRE (0x6U << 2)
#define SPCI_MEM_ATTR_DEVICE_GRE (0x7U << 2)
#define SPCI_MEM_ATTR_NORMAL_MEMORY_UNCACHED (0x9U << 2)
#define SPCI_MEM_ATTR_NORMAL_MEMORY_CACHED_WB (0xBU << 2)
#define SPCI_MEM_ATTR_INNER_SHAREABLE (0x3U << 0)

/**
 * typedef spci_mem_perm8_t - Memory permissions
 *
 * * @SPCI_MEM_ATTR_RO
 *     Request or specify read-only mapping.
 * * @SPCI_MEM_ATTR_RW
 *     Request or allow read-write mapping.
 * * @SPCI_MEM_PERM_NX
 *     Deny executable mapping.
 * * @SPCI_MEM_PERM_X
 *     Request executable mapping.
 */
typedef uint8_t spci_mem_perm8_t;
#define SPCI_MEM_PERM_RO (1U << 0)
#define SPCI_MEM_PERM_RW (1U << 1)
#define SPCI_MEM_PERM_NX (1U << 2)
#define SPCI_MEM_PERM_X (1U << 3)

/**
 * SPCI_MEM_TRANSACTION_FLAG_SHARE_MEMORY - Shere memory transaction flag
 *
 * Used by @SMC_FC_SPCI_MEM_RETRIEVE_RESP to indicate that memory came from
 * @SMC_FC_SPCI_MEM_SHARE.
 */
#define SPCI_MEM_TRANSACTION_FLAG_SHARE_MEMORY (1U << 3)

/**
 * struct spci_memory_access_permissions_descriptor - Endpoint memory permission
 * @endpoint_id:
 *         Endpoint id that @memory_access_permissions apply to. Current
 *         implementation only supports vmid. SPCI spec also support stream
 *         endpoint ids.
 * @memory_access_permissions:
 *         SPCI_MEM_PERM_* values or'ed together (&typedef spci_mem_attr16_t).
 * @reserved_3:
 *         Reserve byte 3 to pad struct size to 4 bytes.
 */
struct spci_memory_access_permissions_descriptor {
	uint16_t endpoint_id;
	spci_mem_perm8_t memory_access_permissions;
	uint8_t reserved_3;
};
STATIC_ASSERT(sizeof(struct spci_memory_access_permissions_descriptor) == 4);

/**
 * struct spci_endpoint_memory_access_descriptor -  descriptor.
 * @perm:  &struct spci_memory_access_permissions_descriptor.
 * @composite_memory_region_descriptor_offset:
 *         Offset of &struct spci_composite_memory_region_descriptor form start
 *         of &struct spci_memory_transaction_descriptor.
 * @reserved_8_15:
 *         Reserved bytes 8-15. Must be 0.
 */
struct spci_endpoint_memory_access_descriptor {
	struct spci_memory_access_permissions_descriptor perm;
	uint32_t composite_memory_region_descriptor_offset;
	uint64_t reserved_8_15;
};
STATIC_ASSERT(sizeof(struct spci_endpoint_memory_access_descriptor) == 16);

/**
 * struct spci_memory_transaction_descriptor - Memory transaction descriptor.
 * @sender_id:
 *         Sender endpoint id.
 * @memory_region_attributes:
 *         SPCI_MEM_ATTR_* values or'ed together (&typedef spci_mem_attr8_t).
 * @reserved_3:
 *         Reserved bytes 3. Must be 0.
 * @flags:
 *         If bit 0 is set, clear memory after unmapping from sender (must be 0
 *         for share as memory will not be unmapped). Bit[4:3]: b'01 for
 *         retrieve response indicates share memory transaction.
 *         All other bit are reserved 0.
 * @handle:
 *         Id of shared memory object. Most be 0 for MEM_SHARE.
 * @tag:   Client allocated tag. Must match original value.
 * @reserved_24_27:
 *         Reserved bytes 24-27. Must be 0.
 *         SPCI 1.0 Beta 1_0-3 reserved bytes 8-11 instead, but should be fixed
 *         in the next version.
 * @endpoint_memory_access_descriptor_count:
 *         Number of entries in @endpoint_memory_access_descriptors. Must be
 *         1 in current implementation. SPCI spec allows more entries.
 * @endpoint_memory_access_descriptors:
 *         Array of @struct spci_endpoint_memory_access_descriptor entries.
 */
struct spci_memory_transaction_descriptor {
	uint16_t sender_id;
	spci_mem_attr8_t memory_region_attributes;
	uint8_t reserved_3;
	uint32_t flags;
	uint64_t handle;
	uint64_t tag;
	uint32_t reserved_24_27;
	uint32_t endpoint_memory_access_descriptor_count;
	struct spci_endpoint_memory_access_descriptor
		endpoint_memory_access_descriptors[];
};
STATIC_ASSERT(sizeof(struct spci_memory_transaction_descriptor) == 32);

/**
 * struct spci_mem_relinquish_descriptor - Relinquish request descriptor.
 * @handle:
 *         Id of shared memory object to relinquish.
 * @flags:
 *         If bit 0 is set clear memory after unmapping from borrower. All other
 *         bit are reserved 0.
 *         Clear is not supported by the current implementation. Does not appear
 *         to have any use if the memory was shared as the content would not be
 *         protected in the first place.
 * @endpoint_count:
 *         Number of entries in @endpoint_array.
 * @endpoint_array:
 *         Array of endpoint ids.
 */
struct spci_mem_relinquish_descriptor {
	uint64_t handle;
	uint32_t flags;
	uint32_t endpoint_count;
	uint16_t endpoint_array[];
};
STATIC_ASSERT(sizeof(struct spci_mem_relinquish_descriptor) == 16);

/**
 * enum spci_error - SPCI error code
 * @SPCI_ERROR_NOT_SUPPORTED:
 *         Operation contained possibly valid parameters not supported by the
 *         current implementation. Does not match SPCI 1.0 Beta 1_0 definition.
 * @SPCI_ERROR_INVALID_PARAMETERS:
 *         Invalid parameters. Conditions function specific.
 * @SPCI_ERROR_NO_MEMORY:
 *         Not enough memory.
 * @SPCI_ERROR_DENIED:
 *         Operation not allowed. Conditions function specific.
 *
 * SPCI 1.0 Beta 1_0 defines other errro codes as well but the current
 * implementation don't use them.
 */
enum spci_error {
	SPCI_ERROR_NOT_SUPPORTED = -1,
	SPCI_ERROR_INVALID_PARAMETERS = -2,
	SPCI_ERROR_NO_MEMORY = -3,
	SPCI_ERROR_DENIED = -6,
};

/**
 * SMC_FC_SPCI_ERROR - SMC error return opcode
 *
 * Register arguments:
 *
 * * w1:     VMID in [31:16], vCPU in [15:0]
 * * w2:     Error code (&enum spci_error)
 */
#define SMC_FC_SPCI_ERROR SMC_FASTCALL_NR_SHARED_MEMORY(0x60)

/**
 * SMC_FC_SPCI_SUCCESS - 32 bit SMC success return opcode
 *
 * Register arguments:
 *
 * * w1:     VMID in [31:16], vCPU in [15:0]
 * * w2-w7:  Function specific
 */
#define SMC_FC_SPCI_SUCCESS SMC_FASTCALL_NR_SHARED_MEMORY(0x61)

/**
 * SMC_FC64_SPCI_SUCCESS - 64 bit SMC success return opcode
 *
 * Register arguments:
 *
 * * w1:             VMID in [31:16], vCPU in [15:0]
 * * w2/x2-w7/x7:    Function specific
 */
#define SMC_FC64_SPCI_SUCCESS SMC_FASTCALL64_NR_SHARED_MEMORY(0x61)

/**
 * SMC_FC_SPCI_VERSION - SMC opcode to return supported spci version
 *
 * Register arguments:
 *
 * * w1:     Major version bit[30:16] and minor version in bit[15:0] supported
 *           by caller. Bit[31] must be 0.
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 * * w2:     Major version bit[30:16], minor version in bit[15:0], bit[31] must
 *           be 0.
 */
#define SMC_FC_SPCI_VERSION SMC_FASTCALL_NR_SHARED_MEMORY(0x63)

/**
 * SPCI_FEATURES_SUPPORT_FRAGMENTS - Bit indicating fragment support
 */
#define SPCI_FEATURES_SUPPORT_FRAGMENTS (1U << 0)

/**
 * SMC_FC_SPCI_FEATURES - SMC opcode to check optional feature support
 *
 * Register arguments:
 *
 * * w1:     SPCI function ID
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 * * w2:     Bit[0]: Support fragments. Bit[1]: Supports timeslicing. Bit[2]:
 *           Supports custom buffers for memory transactions. Bit[31:3] must be
 *           0.
 * * w3:     For SPCI_MEM_RETRIEVE_REQ, bit[7-0]: Number of times receiver can
 *           retrieve each memory region before relinquishing it - 1. For all
 *           other bits and commands: must be 0.
 * or
 *
 * * w0:     SMC_FC_SPCI_ERROR
 * * w2:     SPCI_ERROR_NOT_SUPPORTED if function is not implemented, or
 *           SPCI_ERROR_INVALID_PARAMETERS if function id is not valid.
 */
#define SMC_FC_SPCI_FEATURES SMC_FASTCALL_NR_SHARED_MEMORY(0x64)

/**
 * SMC_FC_SPCI_RXTX_MAP - 32 bit SMC opcode to map message buffers
 *
 * Register arguments:
 *
 * * w1:     TX address
 * * w2:     RX address
 * * w3:     RX/TX page count in bit[5:0]
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_RXTX_MAP SMC_FASTCALL_NR_SHARED_MEMORY(0x66)

/**
 * SMC_FC64_SPCI_RXTX_MAP - 64 bit SMC opcode to map message buffers
 *
 * Register arguments:
 *
 * * x1:     TX address
 * * x2:     RX address
 * * x3:     RX/TX page count in bit[5:0]
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC64_SPCI_RXTX_MAP SMC_FASTCALL64_NR_SHARED_MEMORY(0x66)
#ifdef CONFIG_64BIT
#define SMC_FCZ_SPCI_RXTX_MAP SMC_FC64_SPCI_RXTX_MAP
#else
#define SMC_FCZ_SPCI_RXTX_MAP SMC_FC_SPCI_RXTX_MAP
#endif

/**
 * SMC_FC_SPCI_RXTX_UNMAP - SMC opcode to unmap message buffers
 *
 * Register arguments:
 *
 * * w1:     ID in [31:16]
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_RXTX_UNMAP SMC_FASTCALL_NR_SHARED_MEMORY(0x67)

/**
 * SMC_FC_SPCI_ID_GET - SMC opcode to get endpoint id of caller
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 * * w2:     ID in bit[15:0], bit[31:16] must be 0.
 */
#define SMC_FC_SPCI_ID_GET SMC_FASTCALL_NR_SHARED_MEMORY(0x69)

/**
 * SMC_FC_SPCI_MEM_DONATE - 32 bit SMC opcode to donate memory
 *
 * Not supported.
 */
#define SMC_FC_SPCI_MEM_DONATE SMC_FASTCALL_NR_SHARED_MEMORY(0x71)

/**
 * SMC_FC_SPCI_MEM_LEND - 32 bit SMC opcode to lend memory
 *
 * Not currently supported.
 */
#define SMC_FC_SPCI_MEM_LEND SMC_FASTCALL_NR_SHARED_MEMORY(0x72)

/**
 * SMC_FC_SPCI_MEM_SHARE - 32 bit SMC opcode to share memory
 *
 * Register arguments:
 *
 * * w1:     Total length
 * * w2:     Fragment length
 * * w3:     Address
 * * w4:     Page count
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 * * w2/w3:  Handle
 *
 * or
 *
 * * w0:     &SMC_FC_SPCI_MEM_FRAG_RX
 * * w1-:    See &SMC_FC_SPCI_MEM_FRAG_RX
 *
 * or
 *
 * * w0:     SMC_FC_SPCI_ERROR
 * * w2:     Error code (&enum spci_error)
 */
#define SMC_FC_SPCI_MEM_SHARE SMC_FASTCALL_NR_SHARED_MEMORY(0x73)

/**
 * SMC_FC64_SPCI_MEM_SHARE - 64 bit SMC opcode to share memory
 *
 * Register arguments:
 *
 * * w1:     Total length
 * * w2:     Fragment length
 * * x3:     Address
 * * w4:     Page count
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 * * w2/w3:  Handle
 *
 * or
 *
 * * w0:     &SMC_FC_SPCI_MEM_FRAG_RX
 * * w1-:    See &SMC_FC_SPCI_MEM_FRAG_RX
 *
 * or
 *
 * * w0:     SMC_FC_SPCI_ERROR
 * * w2:     Error code (&enum spci_error)
 */
#define SMC_FC64_SPCI_MEM_SHARE SMC_FASTCALL64_NR_SHARED_MEMORY(0x73)

/**
 * SMC_FC_SPCI_MEM_RETRIEVE_REQ - 32 bit SMC opcode to retrieve shared memory
 *
 * Register arguments:
 *
 * * w1:     Total length
 * * w2:     Fragment length
 * * w3:     Address
 * * w4:     Page count
 *
 * Return:
 * * w0:             &SMC_FC_SPCI_MEM_RETRIEVE_RESP
 * * w1/x1-w5/x5:    See &SMC_FC_SPCI_MEM_RETRIEVE_RESP
 */
#define SMC_FC_SPCI_MEM_RETRIEVE_REQ SMC_FASTCALL_NR_SHARED_MEMORY(0x74)

/**
 * SMC_FC64_SPCI_MEM_RETRIEVE_REQ - 64 bit SMC opcode to retrieve shared memory
 *
 * Register arguments:
 *
 * * w1:     Total length
 * * w2:     Fragment length
 * * x3:     Address
 * * w4:     Page count
 *
 * Return:
 * * w0:             &SMC_FC_SPCI_MEM_RETRIEVE_RESP
 * * w1/x1-w5/x5:    See &SMC_FC_SPCI_MEM_RETRIEVE_RESP
 */
#define SMC_FC64_SPCI_MEM_RETRIEVE_REQ SMC_FASTCALL64_NR_SHARED_MEMORY(0x74)

/**
 * SMC_FC_SPCI_MEM_RETRIEVE_RESP - Retrieve 32 bit SMC return opcode
 *
 * Register arguments:
 *
 * * w1:     Total length
 * * w2:     Fragment length
 */
#define SMC_FC_SPCI_MEM_RETRIEVE_RESP SMC_FASTCALL_NR_SHARED_MEMORY(0x75)

/**
 * SMC_FC_SPCI_MEM_RELINQUISH - SMC opcode to relinquish shared memory
 *
 * Input in &struct spci_mem_relinquish_descriptor format in message buffer.
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_MEM_RELINQUISH SMC_FASTCALL_NR_SHARED_MEMORY(0x76)

/**
 * SMC_FC_SPCI_MEM_RECLAIM - SMC opcode to reclaim shared memory
 *
 * Register arguments:
 *
 * * w1/w2:  Handle
 * * w3:     Flags
 *
 * Return:
 * * w0:     &SMC_FC_SPCI_SUCCESS
 */
#define SMC_FC_SPCI_MEM_RECLAIM SMC_FASTCALL_NR_SHARED_MEMORY(0x77)

/**
 * SMC_FC_SPCI_MEM_FRAG_RX - SMC opcode to request next fragment.
 *
 * Register arguments:
 *
 * * w1/w2:  Cookie
 * * w3:     Fragment offset.
 * * w4:     Endpoint id ID in [31:16], if client is hypervisor.
 *
 * Return:
 * * w0:             &SMC_FC_SPCI_MEM_FRAG_TX
 * * w1/x1-w5/x5:    See &SMC_FC_SPCI_MEM_FRAG_TX
 */
#define SMC_FC_SPCI_MEM_FRAG_RX SMC_FASTCALL_NR_SHARED_MEMORY(0x7A)

/**
 * SMC_FC_SPCI_MEM_FRAG_TX - SMC opcode to transmit next fragment
 *
 * Register arguments:
 *
 * * w1/w2:  Cookie
 * * w3:     Fragment length.
 * * w4:     Sender endpoint id ID in [31:16], if client is hypervisor.
 *
 * Return:
 * * w0:             &SMC_FC_SPCI_MEM_FRAG_TX, &SMC_FC_SPCI_MEM_RETRIEVE_RESP
 *                   or &SMC_FC_SPCI_SUCCESS.
 * * w1/x1-w5/x5:    See opcode in w0.
 */
#define SMC_FC_SPCI_MEM_FRAG_TX SMC_FASTCALL_NR_SHARED_MEMORY(0x7B)
