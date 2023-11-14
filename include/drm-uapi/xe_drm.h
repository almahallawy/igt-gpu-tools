/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DRM_H_
#define _XE_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 */

/**
 * DOC: uevent generated by xe on it's pci node.
 *
 * DRM_XE_RESET_FAILED_UEVENT - Event is generated when attempt to reset gt
 * fails. The value supplied with the event is always "NEEDS_RESET".
 * Additional information supplied is tile id and gt id of the gt unit for
 * which reset has failed.
 */
#define DRM_XE_RESET_FAILED_UEVENT "DEVICE_STATUS"

/**
 * struct xe_user_extension - Base class for defining a chain of extensions
 *
 * Many interfaces need to grow over time. In most cases we can simply
 * extend the struct and have userspace pass in more data. Another option,
 * as demonstrated by Vulkan's approach to providing extensions for forward
 * and backward compatibility, is to use a list of optional structs to
 * provide those extra details.
 *
 * The key advantage to using an extension chain is that it allows us to
 * redefine the interface more easily than an ever growing struct of
 * increasing complexity, and for large parts of that interface to be
 * entirely optional. The downside is more pointer chasing; chasing across
 * the boundary with pointers encapsulated inside u64.
 *
 * Example chaining:
 *
 * .. code-block:: C
 *
 *	struct xe_user_extension ext3 {
 *		.next_extension = 0, // end
 *		.name = ...,
 *	};
 *	struct xe_user_extension ext2 {
 *		.next_extension = (uintptr_t)&ext3,
 *		.name = ...,
 *	};
 *	struct xe_user_extension ext1 {
 *		.next_extension = (uintptr_t)&ext2,
 *		.name = ...,
 *	};
 *
 * Typically the struct xe_user_extension would be embedded in some uAPI
 * struct, and in this case we would feed it the head of the chain(i.e ext1),
 * which would then apply all of the above extensions.
 *
 */
struct xe_user_extension {
	/**
	 * @next_extension:
	 *
	 * Pointer to the next struct xe_user_extension, or zero if the end.
	 */
	__u64 next_extension;

	/**
	 * @name: Name of the extension.
	 *
	 * Note that the name here is just some integer.
	 *
	 * Also note that the name space for this is not global for the whole
	 * driver, but rather its scope/meaning is limited to the specific piece
	 * of uAPI which has embedded the struct xe_user_extension.
	 */
	__u32 name;

	/**
	 * @pad: MBZ
	 *
	 * All undefined bits must be zero.
	 */
	__u32 pad;
};

/*
 * xe specific ioctls.
 *
 * The device specific ioctl range is [DRM_COMMAND_BASE, DRM_COMMAND_END) ie
 * [0x40, 0xa0) (a0 is excluded). The numbers below are defined as offset
 * against DRM_COMMAND_BASE and should be between [0x0, 0x60).
 */
#define DRM_XE_DEVICE_QUERY		0x00
#define DRM_XE_GEM_CREATE		0x01
#define DRM_XE_GEM_MMAP_OFFSET		0x02
#define DRM_XE_VM_CREATE		0x03
#define DRM_XE_VM_DESTROY		0x04
#define DRM_XE_VM_BIND			0x05
#define DRM_XE_EXEC			0x06
#define DRM_XE_EXEC_QUEUE_CREATE	0x07
#define DRM_XE_EXEC_QUEUE_DESTROY	0x08
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY	0x09
#define DRM_XE_EXEC_QUEUE_GET_PROPERTY	0x0a
#define DRM_XE_WAIT_USER_FENCE		0x0b
/* Must be kept compact -- no holes */

#define DRM_IOCTL_XE_DEVICE_QUERY		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_DEVICE_QUERY, struct drm_xe_device_query)
#define DRM_IOCTL_XE_GEM_CREATE			DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_GEM_CREATE, struct drm_xe_gem_create)
#define DRM_IOCTL_XE_GEM_MMAP_OFFSET		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_GEM_MMAP_OFFSET, struct drm_xe_gem_mmap_offset)
#define DRM_IOCTL_XE_VM_CREATE			DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_VM_CREATE, struct drm_xe_vm_create)
#define DRM_IOCTL_XE_VM_DESTROY			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_VM_DESTROY, struct drm_xe_vm_destroy)
#define DRM_IOCTL_XE_VM_BIND			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_VM_BIND, struct drm_xe_vm_bind)
#define DRM_IOCTL_XE_EXEC			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_EXEC, struct drm_xe_exec)
#define DRM_IOCTL_XE_EXEC_QUEUE_CREATE		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_CREATE, struct drm_xe_exec_queue_create)
#define DRM_IOCTL_XE_EXEC_QUEUE_DESTROY		DRM_IOW(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_DESTROY, struct drm_xe_exec_queue_destroy)
#define DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY	DRM_IOW(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_SET_PROPERTY, struct drm_xe_exec_queue_set_property)
#define DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY	DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_GET_PROPERTY, struct drm_xe_exec_queue_get_property)
#define DRM_IOCTL_XE_WAIT_USER_FENCE		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_WAIT_USER_FENCE, struct drm_xe_wait_user_fence)

/** struct drm_xe_engine_class_instance - instance of an engine class */
struct drm_xe_engine_class_instance {
#define DRM_XE_ENGINE_CLASS_RENDER		0
#define DRM_XE_ENGINE_CLASS_COPY		1
#define DRM_XE_ENGINE_CLASS_VIDEO_DECODE	2
#define DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE	3
#define DRM_XE_ENGINE_CLASS_COMPUTE		4
	/*
	 * Kernel only classes (not actual hardware engine class). Used for
	 * creating ordered queues of VM bind operations.
	 */
#define DRM_XE_ENGINE_CLASS_VM_BIND_ASYNC	5
#define DRM_XE_ENGINE_CLASS_VM_BIND_SYNC	6
	__u16 engine_class;

	__u16 engine_instance;
	__u16 gt_id;
	/** @pad: MBZ */
	__u16 pad;
};

/**
 * enum drm_xe_memory_class - Supported memory classes.
 */
enum drm_xe_memory_class {
	/** @DRM_XE_MEM_REGION_CLASS_SYSMEM: Represents system memory. */
	DRM_XE_MEM_REGION_CLASS_SYSMEM = 0,
	/**
	 * @DRM_XE_MEM_REGION_CLASS_VRAM: On discrete platforms, this
	 * represents the memory that is local to the device, which we
	 * call VRAM. Not valid on integrated platforms.
	 */
	DRM_XE_MEM_REGION_CLASS_VRAM
};

/**
 * struct drm_xe_query_mem_region - Describes some region as known to
 * the driver.
 */
struct drm_xe_query_mem_region {
	/**
	 * @mem_class: The memory class describing this region.
	 *
	 * See enum drm_xe_memory_class for supported values.
	 */
	__u16 mem_class;
	/**
	 * @instance: The instance for this region.
	 *
	 * The @mem_class and @instance taken together will always give
	 * a unique pair.
	 */
	__u16 instance;
	/** @pad: MBZ */
	__u32 pad;
	/**
	 * @min_page_size: Min page-size in bytes for this region.
	 *
	 * When the kernel allocates memory for this region, the
	 * underlying pages will be at least @min_page_size in size.
	 *
	 * Important note: When userspace allocates a GTT address which
	 * can point to memory allocated from this region, it must also
	 * respect this minimum alignment. This is enforced by the
	 * kernel.
	 */
	__u32 min_page_size;
	/**
	 * @total_size: The usable size in bytes for this region.
	 */
	__u64 total_size;
	/**
	 * @used: Estimate of the memory used in bytes for this region.
	 *
	 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable
	 * accounting.  Without this the value here will always equal
	 * zero.
	 */
	__u64 used;
	/**
	 * @cpu_visible_size: How much of this region can be CPU
	 * accessed, in bytes.
	 *
	 * This will always be <= @total_size, and the remainder (if
	 * any) will not be CPU accessible. If the CPU accessible part
	 * is smaller than @total_size then this is referred to as a
	 * small BAR system.
	 *
	 * On systems without small BAR (full BAR), the probed_size will
	 * always equal the @total_size, since all of it will be CPU
	 * accessible.
	 *
	 * Note this is only tracked for DRM_XE_MEM_REGION_CLASS_VRAM
	 * regions (for other types the value here will always equal
	 * zero).
	 */
	__u64 cpu_visible_size;
	/**
	 * @cpu_visible_used: Estimate of CPU visible memory used, in
	 * bytes.
	 *
	 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable
	 * accounting. Without this the value here will always equal
	 * zero.  Note this is only currently tracked for
	 * DRM_XE_MEM_REGION_CLASS_VRAM regions (for other types the value
	 * here will always be zero).
	 */
	__u64 cpu_visible_used;
	/** @reserved: MBZ */
	__u64 reserved[6];
};

/**
 * struct drm_xe_query_engine_cycles - correlate CPU and GPU timestamps
 *
 * If a query is made with a struct drm_xe_device_query where .query is equal to
 * DRM_XE_DEVICE_QUERY_ENGINE_CYCLES, then the reply uses struct drm_xe_query_engine_cycles
 * in .data. struct drm_xe_query_engine_cycles is allocated by the user and
 * .data points to this allocated structure.
 *
 * The query returns the engine cycles and the frequency that can
 * be used to calculate the engine timestamp. In addition the
 * query returns a set of cpu timestamps that indicate when the command
 * streamer cycle count was captured.
 */
struct drm_xe_query_engine_cycles {
	/**
	 * @eci: This is input by the user and is the engine for which command
	 * streamer cycles is queried.
	 */
	struct drm_xe_engine_class_instance eci;

	/**
	 * @clockid: This is input by the user and is the reference clock id for
	 * CPU timestamp. For definition, see clock_gettime(2) and
	 * perf_event_open(2). Supported clock ids are CLOCK_MONOTONIC,
	 * CLOCK_MONOTONIC_RAW, CLOCK_REALTIME, CLOCK_BOOTTIME, CLOCK_TAI.
	 */
	__s32 clockid;

	/** @width: Width of the engine cycle counter in bits. */
	__u32 width;

	/**
	 * @engine_cycles: Engine cycles as read from its register
	 * at 0x358 offset.
	 */
	__u64 engine_cycles;

	/** @engine_frequency: Frequency of the engine cycles in Hz. */
	__u64 engine_frequency;

	/**
	 * @cpu_timestamp: CPU timestamp in ns. The timestamp is captured before
	 * reading the engine_cycles register using the reference clockid set by the
	 * user.
	 */
	__u64 cpu_timestamp;

	/**
	 * @cpu_delta: Time delta in ns captured around reading the lower dword
	 * of the engine_cycles register.
	 */
	__u64 cpu_delta;
};

/**
 * struct drm_xe_query_mem_usage - describe memory regions and usage
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_MEM_USAGE, then the reply uses
 * struct drm_xe_query_mem_usage in .data.
 */
struct drm_xe_query_mem_usage {
	/** @num_regions: number of memory regions returned in @regions */
	__u32 num_regions;
	/** @pad: MBZ */
	__u32 pad;
	/** @regions: The returned regions for this device */
	struct drm_xe_query_mem_region regions[];
};

/**
 * struct drm_xe_query_config - describe the device configuration
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_CONFIG, then the reply uses
 * struct drm_xe_query_config in .data.
 *
 */
struct drm_xe_query_config {
	/** @num_params: number of parameters returned in info */
	__u32 num_params;

	/** @pad: MBZ */
	__u32 pad;

#define DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID	0
#define DRM_XE_QUERY_CONFIG_FLAGS			1
	#define DRM_XE_QUERY_CONFIG_FLAGS_HAS_VRAM		(0x1 << 0)
#define DRM_XE_QUERY_CONFIG_MIN_ALIGNMENT		2
#define DRM_XE_QUERY_CONFIG_VA_BITS			3
#define DRM_XE_QUERY_CONFIG_MAX_EXEC_QUEUE_PRIORITY	4
	/** @info: array of elements containing the config info */
	__u64 info[];
};

/**
 * struct drm_xe_query_gt - describe an individual GT.
 *
 * To be used with drm_xe_query_gt_list, which will return a list with all the
 * existing GT individual descriptions.
 * Graphics Technology (GT) is a subset of a GPU/tile that is responsible for
 * implementing graphics and/or media operations.
 */
struct drm_xe_query_gt {
#define DRM_XE_QUERY_GT_TYPE_MAIN		0
#define DRM_XE_QUERY_GT_TYPE_MEDIA		1
	/** @type: GT type: Main or Media */
	__u16 type;
	/** @gt_id: Unique ID of this GT within the PCI Device */
	__u16 gt_id;
	/** @clock_freq: A clock frequency for timestamp */
	__u32 clock_freq;
	/**
	 * @near_mem_regions: Bit mask of instances from
	 * drm_xe_query_mem_usage that are nearest to the current engines
	 * of this GT.
	 */
	__u64 near_mem_regions;
	/**
	 * @far_mem_regions: Bit mask of instances from
	 * drm_xe_query_mem_usage that are far from the engines of this GT.
	 * In general, they have extra indirections when compared to the
	 * @near_mem_regions. For a discrete device this could mean system
	 * memory and memory living in a different tile.
	 */
	__u64 far_mem_regions;
	/** @reserved: Reserved */
	__u64 reserved[8];
};

/**
 * struct drm_xe_query_gt_list - A list with GT description items.
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_GT_LIST, then the reply uses struct
 * drm_xe_query_gt_list in .data.
 */
struct drm_xe_query_gt_list {
	/** @num_gt: number of GT items returned in gt_list */
	__u32 num_gt;
	/** @pad: MBZ */
	__u32 pad;
	/** @gt_list: The GT list returned for this device */
	struct drm_xe_query_gt gt_list[];
};

/**
 * struct drm_xe_query_topology_mask - describe the topology mask of a GT
 *
 * This is the hardware topology which reflects the internal physical
 * structure of the GPU.
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_GT_TOPOLOGY, then the reply uses
 * struct drm_xe_query_topology_mask in .data.
 */
struct drm_xe_query_topology_mask {
	/** @gt_id: GT ID the mask is associated with */
	__u16 gt_id;

	/*
	 * To query the mask of Dual Sub Slices (DSS) available for geometry
	 * operations. For example a query response containing the following
	 * in mask:
	 *   DSS_GEOMETRY    ff ff ff ff 00 00 00 00
	 * means 32 DSS are available for geometry.
	 */
#define DRM_XE_TOPO_DSS_GEOMETRY	(1 << 0)
	/*
	 * To query the mask of Dual Sub Slices (DSS) available for compute
	 * operations. For example a query response containing the following
	 * in mask:
	 *   DSS_COMPUTE    ff ff ff ff 00 00 00 00
	 * means 32 DSS are available for compute.
	 */
#define DRM_XE_TOPO_DSS_COMPUTE		(1 << 1)
	/*
	 * To query the mask of Execution Units (EU) available per Dual Sub
	 * Slices (DSS). For example a query response containing the following
	 * in mask:
	 *   EU_PER_DSS    ff ff 00 00 00 00 00 00
	 * means each DSS has 16 EU.
	 */
#define DRM_XE_TOPO_EU_PER_DSS		(1 << 2)
	/** @type: type of mask */
	__u16 type;

	/** @num_bytes: number of bytes in requested mask */
	__u32 num_bytes;

	/** @mask: little-endian mask of @num_bytes */
	__u8 mask[];
};

/**
 * struct drm_xe_device_query - main structure to query device information
 *
 * If size is set to 0, the driver fills it with the required size for the
 * requested type of data to query. If size is equal to the required size,
 * the queried information is copied into data.
 *
 * For example the following code snippet allows retrieving and printing
 * information about the device engines with DRM_XE_DEVICE_QUERY_ENGINES:
 *
 * .. code-block:: C
 *
 *	struct drm_xe_engine_class_instance *hwe;
 *	struct drm_xe_device_query query = {
 *		.extensions = 0,
 *		.query = DRM_XE_DEVICE_QUERY_ENGINES,
 *		.size = 0,
 *		.data = 0,
 *	};
 *	ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query);
 *	hwe = malloc(query.size);
 *	query.data = (uintptr_t)hwe;
 *	ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query);
 *	int num_engines = query.size / sizeof(*hwe);
 *	for (int i = 0; i < num_engines; i++) {
 *		printf("Engine %d: %s\n", i,
 *			hwe[i].engine_class == DRM_XE_ENGINE_CLASS_RENDER ? "RENDER":
 *			hwe[i].engine_class == DRM_XE_ENGINE_CLASS_COPY ? "COPY":
 *			hwe[i].engine_class == DRM_XE_ENGINE_CLASS_VIDEO_DECODE ? "VIDEO_DECODE":
 *			hwe[i].engine_class == DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE ? "VIDEO_ENHANCE":
 *			hwe[i].engine_class == DRM_XE_ENGINE_CLASS_COMPUTE ? "COMPUTE":
 *			"UNKNOWN");
 *	}
 *	free(hwe);
 */
struct drm_xe_device_query {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_DEVICE_QUERY_ENGINES		0
#define DRM_XE_DEVICE_QUERY_MEM_USAGE		1
#define DRM_XE_DEVICE_QUERY_CONFIG		2
#define DRM_XE_DEVICE_QUERY_GT_LIST		3
#define DRM_XE_DEVICE_QUERY_HWCONFIG		4
#define DRM_XE_DEVICE_QUERY_GT_TOPOLOGY		5
#define DRM_XE_DEVICE_QUERY_ENGINE_CYCLES	6
	/** @query: The type of data to query */
	__u32 query;

	/** @size: Size of the queried data */
	__u32 size;

	/** @data: Queried data is placed here */
	__u64 data;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_gem_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/**
	 * @size: Requested size for the object
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 */
	__u64 size;

#define DRM_XE_GEM_CREATE_FLAG_DEFER_BACKING		(0x1 << 24)
#define DRM_XE_GEM_CREATE_FLAG_SCANOUT			(0x1 << 25)
/*
 * When using VRAM as a possible placement, ensure that the corresponding VRAM
 * allocation will always use the CPU accessible part of VRAM. This is important
 * for small-bar systems (on full-bar systems this gets turned into a noop).
 *
 * Note: System memory can be used as an extra placement if the kernel should
 * spill the allocation to system memory, if space can't be made available in
 * the CPU accessible part of VRAM (giving the same behaviour as the i915
 * interface, see I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS).
 *
 * Note: For clear-color CCS surfaces the kernel needs to read the clear-color
 * value stored in the buffer, and on discrete platforms we need to use VRAM for
 * display surfaces, therefore the kernel requires setting this flag for such
 * objects, otherwise an error is thrown on small-bar systems.
 */
#define DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM	(0x1 << 26)
	/**
	 * @flags: Flags, currently a mask of memory instances of where BO can
	 * be placed
	 */
	__u32 flags;

	/**
	 * @vm_id: Attached VM, if any
	 *
	 * If a VM is specified, this BO must:
	 *
	 *  1. Only ever be bound to that VM.
	 *  2. Cannot be exported as a PRIME fd.
	 */
	__u32 vm_id;

	/**
	 * @handle: Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;

	/** @pad: MBZ */
	__u32 pad;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_gem_mmap_offset {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @handle: Handle for the object being mapped. */
	__u32 handle;

	/** @flags: Must be zero */
	__u32 flags;

	/** @offset: The fake offset to use for subsequent mmap call */
	__u64 offset;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/** struct drm_xe_ext_set_property - XE set property extension */
struct drm_xe_ext_set_property {
	/** @base: base user extension */
	struct xe_user_extension base;

	/** @property: property to set */
	__u32 property;

	/** @pad: MBZ */
	__u32 pad;

	/** @value: property value */
	__u64 value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_vm_create {
#define DRM_XE_VM_EXTENSION_SET_PROPERTY	0
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE	(0x1 << 0)
#define DRM_XE_VM_CREATE_FLAG_COMPUTE_MODE	(0x1 << 1)
#define DRM_XE_VM_CREATE_FLAG_ASYNC_DEFAULT	(0x1 << 2)
#define DRM_XE_VM_CREATE_FLAG_FAULT_MODE	(0x1 << 3)
	/** @flags: Flags */
	__u32 flags;

	/** @vm_id: Returned VM ID */
	__u32 vm_id;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_vm_destroy {
	/** @vm_id: VM ID */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_vm_bind_op {
	/**
	 * @obj: GEM object to operate on, MBZ for MAP_USERPTR, MBZ for UNMAP
	 */
	__u32 obj;

	/** @pad: MBZ */
	__u32 pad;

	union {
		/**
		 * @obj_offset: Offset into the object, MBZ for CLEAR_RANGE,
		 * ignored for unbind
		 */
		__u64 obj_offset;

		/** @userptr: user pointer to bind on */
		__u64 userptr;
	};

	/**
	 * @range: Number of bytes from the object to bind to addr, MBZ for UNMAP_ALL
	 */
	__u64 range;

	/** @addr: Address to operate on, MBZ for UNMAP_ALL */
	__u64 addr;

	/**
	 * @tile_mask: Mask for which tiles to create binds for, 0 == All tiles,
	 * only applies to creating new VMAs
	 */
	__u64 tile_mask;

#define DRM_XE_VM_BIND_OP_MAP		0x0
#define DRM_XE_VM_BIND_OP_UNMAP		0x1
#define DRM_XE_VM_BIND_OP_MAP_USERPTR	0x2
#define DRM_XE_VM_BIND_OP_UNMAP_ALL	0x3
#define DRM_XE_VM_BIND_OP_PREFETCH	0x4
	/** @op: Bind operation to perform */
	__u32 op;

#define DRM_XE_VM_BIND_FLAG_READONLY	(0x1 << 0)
#define DRM_XE_VM_BIND_FLAG_ASYNC	(0x1 << 1)
	/*
	 * Valid on a faulting VM only, do the MAP operation immediately rather
	 * than deferring the MAP to the page fault handler.
	 */
#define DRM_XE_VM_BIND_FLAG_IMMEDIATE	(0x1 << 2)
	/*
	 * When the NULL flag is set, the page tables are setup with a special
	 * bit which indicates writes are dropped and all reads return zero.  In
	 * the future, the NULL flags will only be valid for DRM_XE_VM_BIND_OP_MAP
	 * operations, the BO handle MBZ, and the BO offset MBZ. This flag is
	 * intended to implement VK sparse bindings.
	 */
#define DRM_XE_VM_BIND_FLAG_NULL	(0x1 << 3)
	/** @flags: Bind flags */
	__u32 flags;

	/** @mem_region: Memory region to prefetch VMA to, instance not a mask */
	__u32 region;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_vm_bind {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @vm_id: The ID of the VM to bind to */
	__u32 vm_id;

	/**
	 * @exec_queue_id: exec_queue_id, must be of class DRM_XE_ENGINE_CLASS_VM_BIND
	 * and exec queue must have same vm_id. If zero, the default VM bind engine
	 * is used.
	 */
	__u32 exec_queue_id;

	/** @num_binds: number of binds in this IOCTL */
	__u32 num_binds;

	/** @pad: MBZ */
	__u32 pad;

	union {
		/** @bind: used if num_binds == 1 */
		struct drm_xe_vm_bind_op bind;

		/**
		 * @vector_of_binds: userptr to array of struct
		 * drm_xe_vm_bind_op if num_binds > 1
		 */
		__u64 vector_of_binds;
	};

	/** @num_syncs: amount of syncs to wait on */
	__u32 num_syncs;

	/** @pad2: MBZ */
	__u32 pad2;

	/** @syncs: pointer to struct drm_xe_sync array */
	__u64 syncs;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/* For use with DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_GRANULARITY */

/* Monitor 128KB contiguous region with 4K sub-granularity */
#define DRM_XE_ACC_GRANULARITY_128K 0

/* Monitor 2MB contiguous region with 64KB sub-granularity */
#define DRM_XE_ACC_GRANULARITY_2M 1

/* Monitor 16MB contiguous region with 512KB sub-granularity */
#define DRM_XE_ACC_GRANULARITY_16M 2

/* Monitor 64MB contiguous region with 2M sub-granularity */
#define DRM_XE_ACC_GRANULARITY_64M 3

/**
 * struct drm_xe_exec_queue_set_property - exec queue set property
 *
 * Same namespace for extensions as drm_xe_exec_queue_create
 */
struct drm_xe_exec_queue_set_property {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @exec_queue_id: Exec queue ID */
	__u32 exec_queue_id;

#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY			0
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE		1
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_PREEMPTION_TIMEOUT	2
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_PERSISTENCE		3
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_JOB_TIMEOUT		4
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_TRIGGER		5
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_NOTIFY		6
#define DRM_XE_EXEC_QUEUE_SET_PROPERTY_ACC_GRANULARITY		7
	/** @property: property to set */
	__u32 property;

	/** @value: property value */
	__u64 value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_exec_queue_create {
#define DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY               0
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @width: submission width (number BB per exec) for this exec queue */
	__u16 width;

	/** @num_placements: number of valid placements for this exec queue */
	__u16 num_placements;

	/** @vm_id: VM to use for this exec queue */
	__u32 vm_id;

	/** @flags: MBZ */
	__u32 flags;

	/** @exec_queue_id: Returned exec queue ID */
	__u32 exec_queue_id;

	/**
	 * @instances: user pointer to a 2-d array of struct
	 * drm_xe_engine_class_instance
	 *
	 * length = width (i) * num_placements (j)
	 * index = j + i * width
	 */
	__u64 instances;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_exec_queue_get_property {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @exec_queue_id: Exec queue ID */
	__u32 exec_queue_id;

#define DRM_XE_EXEC_QUEUE_GET_PROPERTY_BAN	0
	/** @property: property to get */
	__u32 property;

	/** @value: property value */
	__u64 value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_exec_queue_destroy {
	/** @exec_queue_id: Exec queue ID */
	__u32 exec_queue_id;

	/** @pad: MBZ */
	__u32 pad;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_sync {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_SYNC_FLAG_SYNCOBJ		0x0
#define DRM_XE_SYNC_FLAG_TIMELINE_SYNCOBJ	0x1
#define DRM_XE_SYNC_FLAG_DMA_BUF		0x2
#define DRM_XE_SYNC_FLAG_USER_FENCE		0x3
#define DRM_XE_SYNC_FLAG_SIGNAL		0x10
	__u32 flags;

	/** @pad: MBZ */
	__u32 pad;

	union {
		__u32 handle;

		/**
		 * @addr: Address of user fence. When sync passed in via exec
		 * IOCTL this a GPU address in the VM. When sync passed in via
		 * VM bind IOCTL this is a user pointer. In either case, it is
		 * the users responsibility that this address is present and
		 * mapped when the user fence is signalled. Must be qword
		 * aligned.
		 */
		__u64 addr;
	};

	__u64 timeline_value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

struct drm_xe_exec {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @exec_queue_id: Exec queue ID for the batch buffer */
	__u32 exec_queue_id;

	/** @num_syncs: Amount of struct drm_xe_sync in array. */
	__u32 num_syncs;

	/** @syncs: Pointer to struct drm_xe_sync array. */
	__u64 syncs;

	/**
	 * @address: address of batch buffer if num_batch_buffer == 1 or an
	 * array of batch buffer addresses
	 */
	__u64 address;

	/**
	 * @num_batch_buffer: number of batch buffer in this exec, must match
	 * the width of the engine
	 */
	__u16 num_batch_buffer;

	/** @pad: MBZ */
	__u16 pad[3];

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_wait_user_fence - wait user fence
 *
 * Wait on user fence, XE will wake-up on every HW engine interrupt in the
 * instances list and check if user fence is complete::
 *
 *	(*addr & MASK) OP (VALUE & MASK)
 *
 * Returns to user on user fence completion or timeout.
 */
struct drm_xe_wait_user_fence {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/**
	 * @addr: user pointer address to wait on, must qword aligned
	 */
	__u64 addr;

#define DRM_XE_UFENCE_WAIT_EQ	0
#define DRM_XE_UFENCE_WAIT_NEQ	1
#define DRM_XE_UFENCE_WAIT_GT	2
#define DRM_XE_UFENCE_WAIT_GTE	3
#define DRM_XE_UFENCE_WAIT_LT	4
#define DRM_XE_UFENCE_WAIT_LTE	5
	/** @op: wait operation (type of comparison) */
	__u16 op;

#define DRM_XE_UFENCE_WAIT_FLAG_SOFT_OP	(1 << 0)	/* e.g. Wait on VM bind */
#define DRM_XE_UFENCE_WAIT_FLAG_ABSTIME	(1 << 1)
	/** @flags: wait flags */
	__u16 flags;

	/** @pad: MBZ */
	__u32 pad;

	/** @value: compare value */
	__u64 value;

#define DRM_XE_UFENCE_WAIT_U8		0xffu
#define DRM_XE_UFENCE_WAIT_U16		0xffffu
#define DRM_XE_UFENCE_WAIT_U32		0xffffffffu
#define DRM_XE_UFENCE_WAIT_U64		0xffffffffffffffffu
	/** @mask: comparison mask */
	__u64 mask;
	/**
	 * @timeout: how long to wait before bailing, value in nanoseconds.
	 * Without DRM_XE_UFENCE_WAIT_FLAG_ABSTIME flag set (relative timeout)
	 * it contains timeout expressed in nanoseconds to wait (fence will
	 * expire at now() + timeout).
	 * When DRM_XE_UFENCE_WAIT_FLAG_ABSTIME flat is set (absolute timeout) wait
	 * will end at timeout (uses system MONOTONIC_CLOCK).
	 * Passing negative timeout leads to neverending wait.
	 *
	 * On relative timeout this value is updated with timeout left
	 * (for restarting the call in case of signal delivery).
	 * On absolute timeout this value stays intact (restarted call still
	 * expire at the same point of time).
	 */
	__s64 timeout;

	/**
	 * @num_engines: number of engine instances to wait on, must be zero
	 * when DRM_XE_UFENCE_WAIT_FLAG_SOFT_OP set
	 */
	__u64 num_engines;

	/**
	 * @instances: user pointer to array of drm_xe_engine_class_instance to
	 * wait on, must be NULL when DRM_XE_UFENCE_WAIT_FLAG_SOFT_OP set
	 */
	__u64 instances;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * DOC: XE PMU event config IDs
 *
 * Check 'man perf_event_open' to use the ID's DRM_XE_PMU_XXXX listed in xe_drm.h
 * in 'struct perf_event_attr' as part of perf_event_open syscall to read a
 * particular event.
 *
 * For example to open the DRMXE_PMU_RENDER_GROUP_BUSY(0):
 *
 * .. code-block:: C
 *
 *	struct perf_event_attr attr;
 *	long long count;
 *	int cpu = 0;
 *	int fd;
 *
 *	memset(&attr, 0, sizeof(struct perf_event_attr));
 *	attr.type = type; // eg: /sys/bus/event_source/devices/xe_0000_56_00.0/type
 *	attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED;
 *	attr.use_clockid = 1;
 *	attr.clockid = CLOCK_MONOTONIC;
 *	attr.config = DRM_XE_PMU_RENDER_GROUP_BUSY(0);
 *
 *	fd = syscall(__NR_perf_event_open, &attr, -1, cpu, -1, 0);
 */

/*
 * Top bits of every counter are GT id.
 */
#define __DRM_XE_PMU_GT_SHIFT (56)

#define ___DRM_XE_PMU_OTHER(gt, x) \
	(((__u64)(x)) | ((__u64)(gt) << __DRM_XE_PMU_GT_SHIFT))

#define DRM_XE_PMU_RENDER_GROUP_BUSY(gt)	___DRM_XE_PMU_OTHER(gt, 0)
#define DRM_XE_PMU_COPY_GROUP_BUSY(gt)		___DRM_XE_PMU_OTHER(gt, 1)
#define DRM_XE_PMU_MEDIA_GROUP_BUSY(gt)		___DRM_XE_PMU_OTHER(gt, 2)
#define DRM_XE_PMU_ANY_ENGINE_GROUP_BUSY(gt)	___DRM_XE_PMU_OTHER(gt, 3)

#if defined(__cplusplus)
}
#endif

#endif /* _XE_DRM_H_ */
