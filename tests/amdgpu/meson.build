
amdgpu_progs = []
amdgpu_deps = test_deps
if libdrm_amdgpu.found()
	amdgpu_progs += [ 'amd_abm',
			  'amd_assr',
			  'amd_basic',
			  'amd_bo',
			  'amd_bypass',
			  'amd_color',
			  'amd_cp_dma_misc',
			  'amd_cs_nop',
			  'amd_deadlock',
			  'amd_dp_dsc',
			  'amd_freesync_video_mode',
			  'amd_hotplug',
			  'amd_ilr',
			  'amd_info',
			  'amd_jpeg_dec',
			  'amd_link_settings',
			  'amd_max_bpc',
			  'amd_mem_leak',
			  'amd_mode_switch',
			  'amd_module_load',
			  'amd_pci_unplug',
			  'amd_plane',
			  'amd_prime',
			  'amd_psr',
			  'amd_uvd_dec',
			  'amd_uvd_enc',
			  'amd_vce_dec',
			  'amd_vrr_range',
			]
	amdgpu_deps += libdrm_amdgpu
endif

foreach prog : amdgpu_progs
	test_executables += executable(prog, prog + '.c',
				       dependencies : amdgpu_deps,
				       install_dir : amdgpudir,
				       install_rpath : amdgpudir_rpathdir,
				       install : true)
	test_list += join_paths('amdgpu', prog)
endforeach
