
extern cvar_t	*r_texturebits;			// number of desired texture bits
										// 0 = use framebuffer depth
										// 16 = use 16-bit textures
										// 32 = use 32-bit textures
										// all else = error


// VULKAN
static struct Vk_Image upload_vk_image(const struct Image_Upload_Data* upload_data, VkBool32 isRepTex)
{
	int w = upload_data->base_level_width;
	int h = upload_data->base_level_height;


	unsigned char* buffer = upload_data->buffer;
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	int bytes_per_pixel = 4;

	if (r_texturebits->integer <= 16)
    {

    	VkBool32 has_alpha = qfalse;
	    for (int i = 0; i < w * h; i++) {
		if (upload_data->buffer[i*4 + 3] != 255)  {
			has_alpha = qtrue;
			break;
		}
	    }

		buffer = (unsigned char*) ri.Hunk_AllocateTempMemory( upload_data->buffer_size / 2 );
		format = has_alpha ? VK_FORMAT_B4G4R4A4_UNORM_PACK16 : VK_FORMAT_A1R5G5B5_UNORM_PACK16;
		bytes_per_pixel = 2;
	}
	if (format == VK_FORMAT_A1R5G5B5_UNORM_PACK16) {
		uint16_t* p = (uint16_t*)buffer;
		for (int i = 0; i < upload_data->buffer_size; i += 4, p++) {
			unsigned char r = upload_data->buffer[i+0];
			unsigned char g = upload_data->buffer[i+1];
			unsigned char b = upload_data->buffer[i+2];

			*p = (uint32_t)((b/255.0) * 31.0 + 0.5) |
				((uint32_t)((g/255.0) * 31.0 + 0.5) << 5) |
				((uint32_t)((r/255.0) * 31.0 + 0.5) << 10) |
				(1 << 15);
		}
	} else if (format == VK_FORMAT_B4G4R4A4_UNORM_PACK16) {
		uint16_t* p = (uint16_t*)buffer;
		for (int i = 0; i < upload_data->buffer_size; i += 4, p++) {
			unsigned char r = upload_data->buffer[i+0];
			unsigned char g = upload_data->buffer[i+1];
			unsigned char b = upload_data->buffer[i+2];
			unsigned char a = upload_data->buffer[i+3];

			*p = (uint32_t)((a/255.0) * 15.0 + 0.5) |
				((uint32_t)((r/255.0) * 15.0 + 0.5) << 4) |
				((uint32_t)((g/255.0) * 15.0 + 0.5) << 8) |
				((uint32_t)((b/255.0) * 15.0 + 0.5) << 12);
		}
	}


	struct Vk_Image image = vk_create_image(w, h, format, upload_data->mip_levels, isRepTex);
	vk_upload_image_data(image.handle, w, h, upload_data->mip_levels > 1, buffer, bytes_per_pixel);

	if (bytes_per_pixel == 2)
		ri.Hunk_FreeTempMemory(buffer);

	return image;
}

static void vk_create_image(uint32_t width, uint32_t height, uint32_t mipLevels, VkBool32 repeat_texture, struct Vk_Image* pImg)
{
	// create image
	{
		VkImageCreateInfo desc;
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.format = VK_FORMAT_R8G8B8A8_UNORM;
		desc.extent.width = width;
		desc.extent.height = height;
		desc.extent.depth = 1;
		desc.mipLevels = mipLevels;
		desc.arrayLayers = 1;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		desc.queueFamilyIndexCount = 0;
		desc.pQueueFamilyIndices = NULL;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VK_CHECK(qvkCreateImage(vk.device, &desc, NULL, &pImg->handle));
		allocate_and_bind_image_memory(pImg->handle);
	}


	// create image view
	{
		VkImageViewCreateInfo desc;
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		desc.pNext = NULL;
		desc.flags = 0;
		desc.image = pImg->handle;
		desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
		desc.format = VK_FORMAT_R8G8B8A8_UNORM;

        // the components field allows you to swizzle the color channels around
		desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // The subresourceRange field describes what the image's purpose is
        // and which part of the image should be accessed. 
		desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		desc.subresourceRange.baseMipLevel = 0;
		desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		desc.subresourceRange.baseArrayLayer = 0;
		desc.subresourceRange.layerCount = 1;
		
        VK_CHECK(qvkCreateImageView(vk.device, &desc, NULL, &pImg->view));
	}

	// create associated descriptor set
    // Allocate a descriptor set from the pool. 
    // Note that we have to provide the descriptor set layout that 
    // we defined in the pipeline_layout sample. 
    // This layout describes how the descriptor set is to be allocated.
	{
		VkDescriptorSetAllocateInfo desc;
		desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		desc.pNext = NULL;
		desc.descriptorPool = vk.descriptor_pool;
		desc.descriptorSetCount = 1;
		desc.pSetLayouts = &vk.set_layout;
		
        VK_CHECK(qvkAllocateDescriptorSets(vk.device, &desc, &pImg->descriptor_set));
        ri.Printf(PRINT_ALL, " Allocate Descriptor Sets \n");

        VkBool32 mipmap = (mipLevels > 1) ? VK_TRUE: VK_FALSE;
        
        VkDescriptorImageInfo image_info;
        image_info.sampler = vk_find_sampler(mipmap, repeat_texture);
        image_info.imageView = pImg->view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet descriptor_write;
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = pImg->descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pNext = NULL;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.pImageInfo = &image_info;
        descriptor_write.pBufferInfo = NULL;
        descriptor_write.pTexelBufferView = NULL;

        qvkUpdateDescriptorSets(vk.device, 1, &descriptor_write, 0, NULL);

        // The above steps essentially copy the VkDescriptorBufferInfo
        // to the descriptor, which is likely in the device memory.
        //
        // This buffer info includes the handle to the uniform buffer
        // as well as the offset and size of the data that is accessed
        // in the uniform buffer. In this case, the uniform buffer 
        // contains only the MVP transform, so the offset is 0 and 
        // the size is the size of the MVP.
        
        
        s_CurrentDescriptorSets[s_CurTmu] = pImg->descriptor_set;
	}
}



unsigned int R_SumOfUsedImages( void )
{
	unsigned int i;

	unsigned int total = 0;
	for ( i = 0; i < tr.numImages; i++ )
    {
		if ( tr.images[i]->frameUsed == tr.frameCount )
        {
			total += tr.images[i]->uploadWidth * tr.images[i]->uploadHeight;
		}
	}

	return total;
}



static void bind_image_with_memory(VkImage image)
{
    VkMemoryRequirements memory_requirements;
    qvkGetImageMemoryRequirements(vk.device, image, &memory_requirements);
    
    // Try to find an existing chunk of sufficient capacity.
    uint32_t mask = (memory_requirements.alignment - 1);


    // ensure that memory region has proper alignment
    uint32_t offset_aligned = (StagImg.used + mask) & (~mask);
    uint32_t needed = offset_aligned + memory_requirements.size; 
    
    // ensure that device local memory is enough
    assert (needed <= IMAGE_CHUNK_SIZE);

    StagImg.used = needed;
    
    // To attach memory to a VkImage object created without the VK_IMAGE_CREATE_DISJOINT_BIT set
    //
    // StagImg.devMemStoreImg is the VkDeviceMemory object describing the device memory to attach.
    // offset_aligned is the start offset of the region of memory which is to be bound to the image. 
    // The number of bytes returned in the VkMemoryRequirements::size member in memory,
    // starting from memoryOffset bytes, will be bound to the specified image.

    VK_CHECK(qvkBindImageMemory(vk.device, image, StagImg.devMemStoreImg, offset_aligned));
    // 	for debug info
    ri.Printf(PRINT_ALL, " StagImg.used = %d \n", StagImg.used);

}


static void R_BindAnimatedImage( textureBundle_t *bundle, uint32_t curTMU )
{
	int		index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		updateCurDescriptor( bundle->image[0]->descriptor_set, curTMU);
        //GL_Bind(bundle->image[0]);
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = (int)( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

	updateCurDescriptor( bundle->image[ index ]->descriptor_set , curTMU);
    //GL_Bind(bundle->image[ index ], curTMU);

}


static void vk_createImageViewAndDescriptorSet(VkImage h_image, VkImageView* pView)
{
    VkImageViewCreateInfo desc;
    desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.image = h_image;
    desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    // format is a VkFormat describing the format and type used 
    // to interpret data elements in the image.
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;

    // the components field allows you to swizzle the color channels around
    desc.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    desc.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // The subresourceRange field describes what the image's purpose is
    // and which part of the image should be accessed. 
    //
    // selecting the set of mipmap levels and array layers to be accessible to the view.
    desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.subresourceRange.baseMipLevel = 0;
    desc.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    desc.subresourceRange.baseArrayLayer = 0;
    desc.subresourceRange.layerCount = 1;
    // Image objects are not directly accessed by pipeline shaders for reading or writing image data.
    // Instead, image views representing contiguous ranges of the image subresources and containing
    // additional metadata are used for that purpose. Views must be created on images of compatible
    // types, and must represent a valid subset of image subresources.
    //
    // Some of the image creation parameters are inherited by the view. In particular, image view
    // creation inherits the implicit parameter usage specifying the allowed usages of the image 
    // view that, by default, takes the value of the corresponding usage parameter specified in
    // VkImageCreateInfo at image creation time.
    //
    // This implicit parameter can be overriden by chaining a VkImageViewUsageCreateInfo structure
    // through the pNext member to VkImageViewCreateInfo.
    VK_CHECK(qvkCreateImageView(vk.device, &desc, NULL, pView));
}



static void vk_upload_image_data(VkImage image, uint32_t width, uint32_t height, const unsigned char* pPixels)
{

    VkBufferImageCopy regions[12];
    uint32_t curLevel = 0;

    uint32_t buffer_size = 0;

    while (1)
    {
	    VkBufferImageCopy region;
	    region.bufferOffset = buffer_size;
	    region.bufferRowLength = 0;
	    region.bufferImageHeight = 0;
	    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	    region.imageSubresource.mipLevel = curLevel;
	    region.imageSubresource.baseArrayLayer = 0;
	    region.imageSubresource.layerCount = 1;
	    region.imageOffset.x = 0;
	    region.imageOffset.y = 0;
	    region.imageOffset.z = 0;

	    region.imageExtent.width = width;
	    region.imageExtent.height = height;
	    region.imageExtent.depth = 1;

	    regions[curLevel] = region;
	    curLevel++;

	    buffer_size += width * height * 4;

	    if ((width == 1) && (height == 1))
		    break;

	    width >>= 1;
	    if (width == 0) 
		    width = 1;

	    height >>= 1;
	    if (height == 0)
		    height = 1;
    }

    //max mipmap buffer size
    assert(buffer_size <= StagImg.buf_size);

    void* data;
    
    VK_CHECK(qvkMapMemory(vk.device, StagImg.devMemMappable, 0, VK_WHOLE_SIZE, 0, &data));

    memcpy(data, pPixels, buffer_size);

    qvkUnmapMemory(vk.device, StagImg.devMemMappable);
    
    
    vk_stagBufferToDeviceLocalMem(image ,regions ,curLevel);
}


static void vk_uploadSingleImage(VkImage image, uint32_t width, uint32_t height, const unsigned char* pPixels)
{

    VkBufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;

    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;


    const uint32_t buffer_size = width * height * 4;

    void* data;
    VK_CHECK(qvkMapMemory(vk.device, StagImg.devMemMappable, 0, VK_WHOLE_SIZE, 0, &data));
    memcpy(data, pPixels, buffer_size);
    qvkUnmapMemory(vk.device, StagImg.devMemMappable);


    vk_stagBufferToDeviceLocalMem(image, &region, 1);
}


static void get_mvp_transform(float* mvp, const int isProj2D)
{
	if (isProj2D)
    {
		float mvp0 = 2.0f / glConfig.vidWidth;
		float mvp5 = 2.0f / glConfig.vidHeight;

		mvp[0]  =  mvp0; mvp[1]  =  0.0f; mvp[2]  = 0.0f; mvp[3]  = 0.0f;
		mvp[4]  =  0.0f; mvp[5]  =  mvp5; mvp[6]  = 0.0f; mvp[7]  = 0.0f;
		mvp[8]  =  0.0f; mvp[9]  =  0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = -1.0f; mvp[13] = -1.0f; mvp[14] = 0.0f; mvp[15] = 1.0f;
	}
    else
    {
		// update q3's proj matrix (opengl) to vulkan conventions: z - [0, 1] instead of [-1, 1] and invert y direction
		
        MatrixMultiply4x4_SSE(s_modelview_matrix, backEnd.viewParms.projectionMatrix, mvp);
	}
}


model_t* R_AllocModel( void )
{

    ri.Printf( PRINT_ALL, "Allocate Memory for model. \n");

	if ( tr.numModels == MAX_MOD_KNOWN )
    {
        ri.Printf(PRINT_WARNING, "R_AllocModel: MAX_MOD_KNOWN.\n");
		return NULL;
	}

	model_t* mod = ri.Hunk_Alloc( sizeof( model_t ), h_low );
	mod->index = tr.numModels;
	tr.models[tr.numModels] = mod;
	tr.numModels++;

	return mod;
}






	if( ext != NULL )
	{
		uint32_t i;
		// Look for the correct loader and use it
		for( i = 0; i < numModelLoaders; i++ )
		{
			if( !Q_stricmp( ext, modelLoaders[ i ].ext ) )
			{
				// Load
				hModel = modelLoaders[ i ].ModelLoader( localName, mod );
				break;
			}
		}

		// A loader was found
		if( i < numModelLoaders )
		{
			if( !hModel )
			{
				// Loader failed, most likely because the file isn't there;
				// try again without the extension
				orgNameFailed = qtrue;
				orgLoader = i;
				stripExtension( name, localName, MAX_QPATH );
			}
			else
			{
				// Something loaded
				return mod->index;
			}
		}
		else
		{
			ri.Printf( PRINT_WARNING, "RegisterModel: %s not find \n ", name);
		}
	}


static int R_ComputeLOD( trRefEntity_t *ent )
{
    ri.Printf(PRINT_ALL, "\n------ R_ComputeLOD ------\n");
	float flod;
	int lod;

	if ( tr.currentModel->numLods < 2 )
	{
		// model has only 1 LOD level, skip computations and bias
		lod = 0;
	}
	else
	{
		// multiple LODs exist, so compute projected bounding sphere
		// and use that as a criteria for selecting LOD
	    float radius;

		if(tr.currentModel->type == MOD_MDR)
		{
			mdrHeader_t* mdr = (mdrHeader_t *) tr.currentModel->modelData;
			int frameSize = (size_t) (&((mdrFrame_t *)0)->bones[mdr->numBones]);
			mdrFrame_t* mdrframe = (mdrFrame_t *) ((unsigned char *) mdr + mdr->ofsFrames + frameSize * ent->e.frame);
			
			radius = RadiusFromBounds(mdrframe->bounds[0], mdrframe->bounds[1]);
		}
		else
		{
			md3Frame_t* frame = (md3Frame_t *) (( ( unsigned char * ) tr.currentModel->md3[0] ) + tr.currentModel->md3[0]->ofsFrames);

			frame += ent->e.frame;

			radius = RadiusFromBounds( frame->bounds[0], frame->bounds[1] );
		}

        float projectedRadius = ProjectRadius( radius, ent->e.origin, tr.viewParms.projectionMatrix);
		
        if ( projectedRadius != 0 )
		{
			float lodscale = r_lodscale->value;
			if (lodscale > 20)
                lodscale = 20;
			flod = 1.0f - projectedRadius * lodscale;
		}
		else
		{
			// object intersects near view plane, e.g. view weapon
			flod = 0;
		}

		flod *= tr.currentModel->numLods;
		
        lod = (int)(flod);

		if ( lod < 0 )
		{
			lod = 0;
		}
		else if ( lod >= tr.currentModel->numLods )
		{
			lod = tr.currentModel->numLods - 1;
		}
	}

	lod += r_lodbias->integer;
	
	if ( lod >= tr.currentModel->numLods )
		lod = tr.currentModel->numLods - 1;
    else if ( lod < 0 )
		lod = 0;

	return lod;
}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================

void RB_BeginDrawingView (void)
{
	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

	//
	// set the modelview matrix for the viewer
	//

	// ensures that depth writes are enabled for the depth clear
    if(r_fastsky->integer && !( backEnd.refdef.rd.rdflags & RDF_NOWORLDMODEL ))
    {
        #ifndef NDEBUG
        static const float fast_sky_color[4] = { 0.8f, 0.7f, 0.4f, 1.0f };
        #else
        static const float fast_sky_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        #endif
        vk_clearColorAttachments(fast_sky_color);
    }
    

	// VULKAN
    vk_clearDepthStencilAttachments();

	if ( ( backEnd.refdef.rd.rdflags & RDF_HYPERSPACE ) )
	{
		//RB_Hyperspace();
        // A player has predicted a teleport, but hasn't arrived yet
        const float c = ( backEnd.refdef.rd.time & 255 ) / 255.0f;
        const float color[4] = { c, c, c, 1 };

        // so short, do we really need this?
	    vk_clearColorAttachments(color);

	    backEnd.isHyperspace = qtrue;
	}
	else
	{
		backEnd.isHyperspace = qfalse;
	}
}
*/

/*
void vk_clear_attachments(VkBool32 clear_depth_stencil, VkBool32 clear_color, float* color)
{

	if (!clear_depth_stencil && !clear_color)
		return;

	VkClearAttachment attachments[2];
	uint32_t attachment_count = 0;

	if (clear_depth_stencil)
    {
		attachments[0].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		attachments[0].clearValue.depthStencil.depth = 1.0f;

		if (r_shadows->integer == 2) {
			attachments[0].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			attachments[0].clearValue.depthStencil.stencil = 0;
		}
		attachment_count = 1;
	}

	if (clear_color)
    {
		attachments[attachment_count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		attachments[attachment_count].colorAttachment = 0;
		attachments[attachment_count].clearValue.color.float32[0] = color[0];
  		attachments[attachment_count].clearValue.color.float32[1] = color[1];
		attachments[attachment_count].clearValue.color.float32[2] = color[2];
		attachments[attachment_count].clearValue.color.float32[3] = color[3];
		attachment_count++;
	}

	VkClearRect clear_rect[2];
	clear_rect[0].rect = get_scissor_rect();
	clear_rect[0].baseArrayLayer = 0;
	clear_rect[0].layerCount = 1;
	int rect_count = 1;

	// Split viewport rectangle into two non-overlapping rectangles.
	// It's a HACK to prevent Vulkan validation layer's performance warning:
	//		"vkCmdClearAttachments() issued on command buffer object XXX prior to any Draw Cmds.
	//		 It is recommended you use RenderPass LOAD_OP_CLEAR on Attachments prior to any Draw."
	// 
	// NOTE: we don't use LOAD_OP_CLEAR for color attachment when we begin renderpass
	// since at that point we don't know whether we need color buffer clear (usually we don't).
	if (clear_color) {
		uint32_t h = clear_rect[0].rect.extent.height / 2;
		clear_rect[0].rect.extent.height = h;
		clear_rect[1] = clear_rect[0];
		clear_rect[1].rect.offset.y = h;
		rect_count = 2;
	}

	qvkCmdClearAttachments(vk.command_buffer, attachment_count, attachments, rect_count, clear_rect);
}
*/


void vk_createGeometryBuffers(void)
{

    // The buffer has been created, but it doesn't actually have any memory
    // assigned to it yet.

    VkMemoryRequirements vb_memory_requirements;
    qvkGetBufferMemoryRequirements(vk.device, shadingDat.vertex_buffer, &vb_memory_requirements);

    VkMemoryRequirements ib_memory_requirements;
    qvkGetBufferMemoryRequirements(vk.device, shadingDat.index_buffer, &ib_memory_requirements);

    const VkDeviceSize mask = (ib_memory_requirements.alignment - 1);

    const VkDeviceSize idxBufOffset = (vb_memory_requirements.size + mask) & (~mask);

    uint32_t memory_type_bits = vb_memory_requirements.memoryTypeBits & ib_memory_requirements.memoryTypeBits;

    VkMemoryAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.allocationSize = idxBufOffset + ib_memory_requirements.size;
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT bit specifies that memory allocated with
    // this type can be mapped for host access using vkMapMemory.
    //
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT bit specifies that the host cache
    // management commands vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges
    // are not needed to flush host writes to the device or make device writes visible
    // to the host, respectively.
    alloc_info.memoryTypeIndex = find_memory_type(memory_type_bits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    ri.Printf(PRINT_ALL, " Allocate device memory: shadingDat.geometry_buffer_memory(%ld bytes). \n",
            alloc_info.allocationSize);

    VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &shadingDat.geometry_buffer_memory));

    // To attach memory to a buffer object
    // vk.device is the logical device that owns the buffer and memory.
    // vertex_buffer/index_buffer is the buffer to be attached to memory.
    // 
    // geometry_buffer_memory is a VkDeviceMemory object describing the 
    // device memory to attach.
    // 
    // idxBufOffset is the start offset of the region of memory 
    // which is to be bound to the buffer.
    // 
    // The number of bytes returned in the VkMemoryRequirements::size member
    // in memory, starting from memoryOffset bytes, will be bound to the 
    // specified buffer.
    qvkBindBufferMemory(vk.device, shadingDat.vertex_buffer, shadingDat.geometry_buffer_memory, 0);
    qvkBindBufferMemory(vk.device, shadingDat.index_buffer, shadingDat.geometry_buffer_memory, idxBufOffset);

    // Host Access to Device Memory Objects
    // Memory objects created with vkAllocateMemory are not directly host accessible.
    // Memory objects created with the memory property VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // are considered mappable. 
    // Memory objects must be mappable in order to be successfully mapped on the host
    // VK_WHOLE_SIZE: map from offset to the end of the allocation.
    // &data points to a pointer in which is returned a host-accessible pointer
    // to the beginning of the mapped range. This pointer minus offset must be
    // aligned to at least VkPhysicalDeviceLimits::minMemoryMapAlignment.
    void* data;
    VK_CHECK(qvkMapMemory(vk.device, shadingDat.geometry_buffer_memory, 0, VK_WHOLE_SIZE, 0, &data));
    shadingDat.vertex_buffer_ptr = (unsigned char*)data;
    shadingDat.index_buffer_ptr = (unsigned char*)data + idxBufOffset;
}


static void vk_read_pixels(unsigned char* buffer)
{

    ri.Printf(PRINT_ALL, " vk_read_pixels() \n\n");

	qvkDeviceWaitIdle(vk.device);

	// Create image in host visible memory to serve as a destination for framebuffer pixels.
	VkImageCreateInfo desc;
	desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.imageType = VK_IMAGE_TYPE_2D;
	desc.format = VK_FORMAT_R8G8B8A8_UNORM;
	desc.extent.width = glConfig.vidWidth;
	desc.extent.height = glConfig.vidHeight;
	desc.extent.depth = 1;
	desc.mipLevels = 1;
	desc.arrayLayers = 1;
	desc.samples = VK_SAMPLE_COUNT_1_BIT;
	desc.tiling = VK_IMAGE_TILING_LINEAR;
	desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
	VK_CHECK(qvkCreateImage(vk.device, &desc, NULL, &image));

	VkMemoryRequirements memory_requirements;
	qvkGetImageMemoryRequirements(vk.device, image, &memory_requirements);
	VkMemoryAllocateInfo alloc_info;
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
    VkDeviceMemory memory;
	VK_CHECK(qvkAllocateMemory(vk.device, &alloc_info, NULL, &memory));
	VK_CHECK(qvkBindImageMemory(vk.device, image, memory, 0));

  
    {

        VkCommandBufferAllocateInfo alloc_info;
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = NULL;
        alloc_info.commandPool = vk.command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer cmdBuf;
        VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &cmdBuf));

        VkCommandBufferBeginInfo begin_info;
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.pNext = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        begin_info.pInheritanceInfo = NULL;

        VK_CHECK(qvkBeginCommandBuffer(cmdBuf, &begin_info));

        record_image_layout_transition(cmdBuf, 
                vk.swapchain_images_array[vk.idx_swapchain_image], 
                VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_MEMORY_READ_BIT, 
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        record_image_layout_transition(cmdBuf, 
                image, VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

        VK_CHECK(qvkEndCommandBuffer(cmdBuf));

        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = NULL;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmdBuf;
        submit_info.signalSemaphoreCount = 0;
        submit_info.pSignalSemaphores = NULL;

        VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));
        VK_CHECK(qvkQueueWaitIdle(vk.queue));
        qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmdBuf);
    }


    //////////////////////////////////////////////////////////////////////
	
    // Check if we can use vkCmdBlitImage for the given 
    // source and destination image formats.
	VkBool32 blit_enabled = 1;
	{
		VkFormatProperties formatProps;
		qvkGetPhysicalDeviceFormatProperties(
                vk.physical_device, vk.surface_format.format, &formatProps );

		if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0)
			blit_enabled = 0;

		qvkGetPhysicalDeviceFormatProperties(
                vk.physical_device, VK_FORMAT_R8G8B8A8_UNORM, &formatProps );

		if ((formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
			blit_enabled = 0;
	}

	if (blit_enabled)
    {
        ri.Printf(PRINT_ALL, " blit_enabled \n\n");

        VkCommandBufferAllocateInfo alloc_info;
    	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    	alloc_info.pNext = NULL;
    	alloc_info.commandPool = vk.command_pool;
    	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    	alloc_info.commandBufferCount = 1;

    	VkCommandBuffer command_buffer;
    	VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &command_buffer));

    	VkCommandBufferBeginInfo begin_info;
    	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    	begin_info.pNext = NULL;
    	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    	begin_info.pInheritanceInfo = NULL;

    	VK_CHECK(qvkBeginCommandBuffer(command_buffer, &begin_info));
        //	recorder(command_buffer);
        {
            VkImageBlit region;
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.mipLevel = 0;
            region.srcSubresource.baseArrayLayer = 0;
            region.srcSubresource.layerCount = 1;

            region.srcOffsets[0].x = 0;
            region.srcOffsets[0].y = 0;
            region.srcOffsets[0].z = 0;

            region.srcOffsets[1].x = glConfig.vidWidth;
            region.srcOffsets[1].y = glConfig.vidHeight;
            region.srcOffsets[1].z = 1;

            region.dstSubresource = region.srcSubresource;
            region.dstOffsets[0] = region.srcOffsets[0];
            region.dstOffsets[1] = region.srcOffsets[1];

            qvkCmdBlitImage(command_buffer,
                    vk.swapchain_images_array[vk.idx_swapchain_image], 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                    VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_NEAREST);
        }

	    VK_CHECK(qvkEndCommandBuffer(command_buffer));

    	VkSubmitInfo submit_info;
    	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    	submit_info.pNext = NULL;
    	submit_info.waitSemaphoreCount = 0;
    	submit_info.pWaitSemaphores = NULL;
    	submit_info.pWaitDstStageMask = NULL;
    	submit_info.commandBufferCount = 1;
    	submit_info.pCommandBuffers = &command_buffer;
    	submit_info.signalSemaphoreCount = 0;
    	submit_info.pSignalSemaphores = NULL;

    	VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));
    	VK_CHECK(qvkQueueWaitIdle(vk.queue));
    	qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &command_buffer);   
	}
    else
    {

	    VkCommandBufferAllocateInfo alloc_info;
    	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    	alloc_info.pNext = NULL;
    	alloc_info.commandPool = vk.command_pool;
    	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    	alloc_info.commandBufferCount = 1;

	    VkCommandBuffer command_buffer;
    	VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &command_buffer));

	    VkCommandBufferBeginInfo begin_info;
    	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    	begin_info.pNext = NULL;
    	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    	begin_info.pInheritanceInfo = NULL;

	    VK_CHECK(qvkBeginCommandBuffer(command_buffer, &begin_info));
	    {
            ////   recorder(command_buffer);
            VkImageCopy region;
            region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.srcSubresource.mipLevel = 0;
            region.srcSubresource.baseArrayLayer = 0;
            region.srcSubresource.layerCount = 1;
            region.srcOffset.x = 0;
            region.srcOffset.y = 0;
            region.srcOffset.z = 0;
            region.dstSubresource = region.srcSubresource;
            region.dstOffset = region.srcOffset;
            region.extent.width = glConfig.vidWidth;
            region.extent.height = glConfig.vidHeight;
            region.extent.depth = 1;

            qvkCmdCopyImage(command_buffer, 
                    vk.swapchain_images_array[vk.idx_swapchain_image], 
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
		
        }
        VK_CHECK(qvkEndCommandBuffer(command_buffer));

	    VkSubmitInfo submit_info;
    	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    	submit_info.pNext = NULL;
    	submit_info.waitSemaphoreCount = 0;
    	submit_info.pWaitSemaphores = NULL;
    	submit_info.pWaitDstStageMask = NULL;
    	submit_info.commandBufferCount = 1;
    	submit_info.pCommandBuffers = &command_buffer;
    	submit_info.signalSemaphoreCount = 0;
    	submit_info.pSignalSemaphores = NULL;

    	VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));
        VK_CHECK(qvkQueueWaitIdle(vk.queue));
    	qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &command_buffer);
	}



	// Copy data from destination image to memory buffer.
	VkImageSubresource subresource;
	subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresource.mipLevel = 0;
	subresource.arrayLayer = 0;
	VkSubresourceLayout layout;
	qvkGetImageSubresourceLayout(vk.device, image, &subresource, &layout);


    // Memory objects created with vkAllocateMemory are not directly host accessible.
    // Memory objects created with the memory property 
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT are considered mappable. 
    // Memory objects must be mappable in order to be successfully 
    // mapped on the host. 
    // To retrieve a host virtual address pointer to 
    // a region of a mappable memory object
    unsigned char* data;
    VK_CHECK(qvkMapMemory(vk.device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data));
	data += layout.offset;

	unsigned char* buffer_ptr = buffer + glConfig.vidWidth * (glConfig.vidHeight - 1) * 4;
	
    int j = 0;
    for (j = 0; j < glConfig.vidHeight; j++)
    {
		memcpy(buffer_ptr, data, glConfig.vidWidth * 4);
		buffer_ptr -= glConfig.vidWidth * 4;
		data += layout.rowPitch;
	}


	if (!blit_enabled)
    {
        ri.Printf(PRINT_ALL, "blit_enabled = 0 \n\n");

		//auto fmt = vk.surface_format.format;
		VkBool32 swizzle_components = 
            (vk.surface_format.format == VK_FORMAT_B8G8R8A8_SRGB ||
             vk.surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             vk.surface_format.format == VK_FORMAT_B8G8R8A8_SNORM);
        
		if (swizzle_components)
        {
			buffer_ptr = buffer;
            unsigned int i = 0;
            unsigned int pixels = glConfig.vidWidth * glConfig.vidHeight;
			for (i = 0; i < pixels; i++)
            {
				unsigned char tmp = buffer_ptr[0];
				buffer_ptr[0] = buffer_ptr[2];
				buffer_ptr[2] = tmp;
				buffer_ptr += 4;
			}
		}
	}
	qvkUnmapMemory(vk.device, memory);
	qvkFreeMemory(vk.device, memory, NULL);

    qvkDestroyImage(vk.device, image, NULL);
}


/*
=================
Setup that culling frustum planes for the current view
=================
*/
static void R_SetupFrustum (viewParms_t * const pViewParams)
{
	
    {
        float ang = pViewParams->fovX * (M_PI / 360.0f);
        float xs = sin( ang );
        float xc = cos( ang );

        float temp1[3];
        float temp2[3];

        VectorScale( pViewParams->or.axis[0], xs, temp1 );
        VectorScale( pViewParams->or.axis[1], xc, temp2);

        VectorAdd(temp1, temp2, pViewParams->frustum[0].normal);
        VectorSubtract(temp1, temp2, pViewParams->frustum[1].normal);
    }
    // VectorMA( tr.viewParms.frustum[0].normal, xc, tr.viewParms.or.axis[1], tr.viewParms.frustum[0].normal );
	//VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[1].normal );
	//VectorMA( tr.viewParms.frustum[1].normal, -xc, tr.viewParms.or.axis[1], tr.viewParms.frustum[1].normal );
   
    {
        float ang = pViewParams->fovY * (M_PI / 360.0f);
        float xs = sin( ang );
        float xc = cos( ang );
        float temp1[3];
        float temp2[3];

        VectorScale( pViewParams->or.axis[0], xs, temp1);
        VectorScale( pViewParams->or.axis[2], xc, temp2);

        VectorAdd(temp1, temp2, pViewParams->frustum[2].normal);
        VectorSubtract(temp1, temp2, pViewParams->frustum[3].normal);
    }

	//VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[2].normal );
	//VectorMA( tr.viewParms.frustum[2].normal, xc, tr.viewParms.or.axis[2], tr.viewParms.frustum[2].normal );
	//VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[3].normal );
	//VectorMA( tr.viewParms.frustum[3].normal, -xc, tr.viewParms.or.axis[2], tr.viewParms.frustum[3].normal );
	
    uint32_t i = 0;
	for (i=0; i < 4; i++)
    {
		pViewParams->frustum[i].type = PLANE_NON_AXIAL;
		pViewParams->frustum[i].dist = DotProduct (pViewParams->or.origin, pViewParams->frustum[i].normal);
		SetPlaneSignbits( &pViewParams->frustum[i] );
	}
}

/*
static void vk_find_pipeline( uint32_t state_bits,
        enum Vk_Shader_Type shader_type,
        enum CullType_t face_culling,
        enum Vk_Shadow_Phase shadow_phase,
        VkBool32 isClippingPlane,
        VkBool32 isMirror,
        VkBool32 isPolygonOffset,
        VkBool32 isLine, 
        VkPipeline *pPl )
{
    uint32_t i = 0;
	for (i = 0; i < s_numPipelines; i++)
    {
		if ( (s_created_ppl[i].par.state_bits == state_bits) &&
             (s_created_ppl[i].par.shader_type == shader_type) &&
			 (s_created_ppl[i].par.face_culling == face_culling) &&
			 (s_created_ppl[i].par.polygon_offset == isPolygonOffset) &&
             (s_created_ppl[i].par.shadow_phase == shadow_phase) &&
			 (s_created_ppl[i].par.clipping_plane == isClippingPlane) &&
			 (s_created_ppl[i].par.mirror == isMirror) &&
             (s_created_ppl[i].par.line_primitives == isLine) )
		{
			*pPl = s_created_ppl[i].pipeline;
            return;
		}
	}

    vk_create_pipeline(state_bits, shader_type, face_culling, shadow_phase, 
            isClippingPlane, isMirror, isPolygonOffset, isLine, pPl);
  
	s_created_ppl[s_numPipelines].pipeline = *pPl;
    s_created_ppl[s_numPipelines].par.shader_type = shader_type;
    s_created_ppl[s_numPipelines].par.state_bits = state_bits;
    s_created_ppl[s_numPipelines].par.face_culling = face_culling;
    s_created_ppl[s_numPipelines].par.shadow_phase = shadow_phase;
    s_created_ppl[s_numPipelines].par.polygon_offset = isPolygonOffset;
    s_created_ppl[s_numPipelines].par.clipping_plane = isClippingPlane;
    s_created_ppl[s_numPipelines].par.mirror = isMirror;
    s_created_ppl[s_numPipelines].par.line_primitives = isLine;

    if (++s_numPipelines >= MAX_VK_PIPELINES)
    {
        // TODO: if not enough, Create new buffer, copy the old to the new buffer
		ri.Error(ERR_DROP, " MAX MUNBERS OF PIPELINES HIT \n");
	}
}

*/

/* sort the array between lo and hi (inclusive)
FIXME: this was lifted and modified from the microsoft lib source...
 */

static void qsortFast ( drawSurf_t * base,  unsigned num, unsigned width )
{
            
    char *mid;                  /* points to middle of subarray */
    char *loguy, *higuy;        /* traveling pointers for partition step */
    unsigned size;              /* size of the sub-array */
    char *lostk[30], *histk[30];
    /* Note: the number of stack entries required is no more than
       1 + log2(size), so 30 is sufficient for any array */

    if (num < 2 || width == 0)
        return;                 /* nothing to do */
    
    /* stack for saving sub-array to be processed */
    int stkptr = 0;                 /* initialize stack */
    
    /* ends of sub-array currently sorting */
    char * lo = (char *)base;
    char * hi = (char *)(base + (num-1));
    /* initialize limits */

    /* this entry point is for pseudo-recursion calling: setting
       lo and hi and jumping to here is like recursion, but stkptr is
       prserved, locals aren't, so we preserve stuff on the stack */
recurse:

    size = (hi - lo) / width + 1;        /* number of el's to sort */

    /* below a certain size, it is faster to use a O(n^2) sorting method */
    if (size <= CUTOFF) {
         shortsort((drawSurf_t *)lo, (drawSurf_t *)hi);
    }
    else
    {
        /* First we pick a partititioning element.  The efficiency of the
           algorithm demands that we find one that is approximately the
           median of the values, but also that we select one fast.  Using
           the first one produces bad performace if the array is already
           sorted, so we use the middle one, which would require a very
           wierdly arranged array for worst case performance.  Testing shows
           that a median-of-three algorithm does not, in general, increase
           performance. */

        mid = lo + (size / 2) * width;      /* find middle element */
        SWAP_DRAW_SURF((drawSurf_t *)mid, (drawSurf_t *)lo); /* swap it to beginning of array */

        /* We now wish to partition the array into three pieces, one
           consisiting of elements <= partition element, one of elements
           equal to the parition element, and one of element >= to it.  This
           is done below; comments indicate conditions established at every
           step. */

        loguy = lo;
        higuy = hi + width;

        /* Note that higuy decreases and loguy increases on every iteration,
           so loop must terminate. */
        for (;;)
        {
            /* lo <= loguy < hi, lo < higuy <= hi + 1,
               A[i] <= A[lo] for lo <= i <= loguy,
               A[i] >= A[lo] for higuy <= i <= hi */

            do  {
                loguy += width;
            } while (loguy <= hi &&  
				( ((drawSurf_t *)loguy)->sort <= ((drawSurf_t *)lo)->sort ) );

            /* lo < loguy <= hi+1, A[i] <= A[lo] for lo <= i < loguy,
               either loguy > hi or A[loguy] > A[lo] */

            do  {
                higuy -= width;
            } while (higuy > lo && 
				( ((drawSurf_t *)higuy)->sort >= ((drawSurf_t *)lo)->sort ) );

            /* lo-1 <= higuy <= hi, A[i] >= A[lo] for higuy < i <= hi,
               either higuy <= lo or A[higuy] < A[lo] */

            if (higuy < loguy)
                break;

            /* if loguy > hi or higuy <= lo, then we would have exited, so
               A[loguy] > A[lo], A[higuy] < A[lo],
               loguy < hi, highy > lo */

            SWAP_DRAW_SURF((drawSurf_t *)loguy, (drawSurf_t *)higuy);

            /* A[loguy] < A[lo], A[higuy] > A[lo]; so condition at top
               of loop is re-established */
        }

        /*     A[i] >= A[lo] for higuy < i <= hi,
               A[i] <= A[lo] for lo <= i < loguy,
               higuy < loguy, lo <= higuy <= hi
           implying:
               A[i] >= A[lo] for loguy <= i <= hi,
               A[i] <= A[lo] for lo <= i <= higuy,
               A[i] = A[lo] for higuy < i < loguy */

        SWAP_DRAW_SURF((drawSurf_t *)lo, (drawSurf_t *)higuy);     /* put partition element in place */

        /* OK, now we have the following:
              A[i] >= A[higuy] for loguy <= i <= hi,
              A[i] <= A[higuy] for lo <= i < higuy
              A[i] = A[lo] for higuy <= i < loguy    */

        /* We've finished the partition, now we want to sort the subarrays
           [lo, higuy-1] and [loguy, hi].
           We do the smaller one first to minimize stack usage.
           We only sort arrays of length 2 or more.*/

        if ( higuy - 1 - lo >= hi - loguy )
        {
            if (lo + width < higuy) {
                lostk[stkptr] = lo;
                histk[stkptr] = higuy - width;
                ++stkptr;
            }                           /* save big recursion for later */

            if (loguy < hi) {
                lo = loguy;
                goto recurse;           /* do small recursion */
            }
        }
        else
        {
            if (loguy < hi) {
                lostk[stkptr] = loguy;
                histk[stkptr] = hi;
                ++stkptr;               /* save big recursion for later */
            }

            if (lo + width < higuy) {
                hi = higuy - width;
                goto recurse;           /* do small recursion */
            }
        }
    }

    /* We have sorted the array, except for any pending sorts on the stack.
       Check if there are any, and do them. */

    --stkptr;
    if (stkptr >= 0) {
        lo = lostk[stkptr];
        hi = histk[stkptr];
        goto recurse;           /* pop subarray from stack */
    }
    else
        return;                 /* all subarrays done */
}

/*
============
FS_ReadFile

Filename are relative to the openarena search path
a null buffer will just return the file length without loading
RF = For Renderer, assuming not .cfg file buffer != NULL
============
*/
long R_ReadFile(const char *filename, char **buffer)
{

    long len = -1;
	fileHandle_t h = 0;

	searchpath_t *search;

	for(search = fs_searchpaths; search; search = search->next)
	{
		//len = FS_FOpenFileReadDir(qpath, search, file, qfalse, qfalse);
        
	if(filename == NULL)
		Com_Error(ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed");

	// qpaths are not supposed to have a leading slash
	if( (filename[0] == '/') || (filename[0] == '\\') )
		filename++;

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if(strstr(filename, ".." ) || strstr(filename, "::"))
	{
		continue;
	}


	h = FS_HandleForFile();
	fsh[h].handleFiles.unique = qfalse;

	// is the element a pak file?
	if(search->pack)
	{
		long hash = FS_HashFileName(filename, search->pack->hashSize);

		if(search->pack->hashTable[hash])
		{
			// disregard if it doesn't match one of the allowed pure pak files
			if( !FS_PakIsPure(search->pack))
			{
		        h = 0;
                len = -1;
		        continue;
			}

			// look through all the pak file elements
			pack_t* pak = search->pack;
			fileInPack_t* pakFile = pak->hashTable[hash];

			do
			{
				// case and separator insensitive comparisons
				if(!FS_FilenameCompare(pakFile->name, filename))
				{
					// found it!

					// mark the pak as having been referenced and mark specifics on cgame and ui
					// shaders, txt, arena files  by themselves do not count as a reference as 
					// these are loaded from all pk3s 
					// from every pk3 file.. 
					len = strlen(filename);

					if (!(pak->referenced & FS_GENERAL_REF))
					{
						if(!FS_IsExt(filename, ".shader", len) &&
						   !FS_IsExt(filename, ".txt", len) &&
						   !FS_IsExt(filename, ".cfg", len) &&
						   Q_stricmp(filename,"qagame.qvm") != 0 && //Never reference qagame because it prevents serverside mods
						   !FS_IsExt(filename, ".config", len) &&
						   !FS_IsExt(filename, ".bot", len) &&
						   !FS_IsExt(filename, ".arena", len) &&
						   !FS_IsExt(filename, ".menu", len) &&
						   Q_stricmp(filename, "vm/qagame.qvm") != 0 &&
						   !strstr(filename, "levelshots"))
						{
							pak->referenced |= FS_GENERAL_REF;
						}
					}

					if(strstr(filename, "cgame.qvm"))
						pak->referenced |= FS_CGAME_REF;
					if(strstr(filename, "ui.qvm"))
						pak->referenced |= FS_UI_REF;

			
					fsh[h].handleFiles.file.z = pak->handle;

					Q_strncpyz(fsh[h].name, filename, sizeof(fsh[h].name));
					fsh[h].zipFile = qtrue;

					// set the file position in the zip file (also sets the current file info)
					unzSetOffset(fsh[h].handleFiles.file.z, pakFile->pos);

					// open the file in the zip
					unzOpenCurrentFile(fsh[h].handleFiles.file.z);
					fsh[h].zipFilePos = pakFile->pos;
					fsh[h].zipFileLen = pakFile->len;

					if(fs_debug->integer)
					{
						Com_Printf("FS_FOpenFileRead: %s (found in '%s')\n", filename, pak->pakFilename);
					}

					len = pakFile->len;
                    goto _PROC_END_;

				}

				pakFile = pakFile->next;
			} while(pakFile != NULL);
		}
	}
	else if(search->dir)
	{
		// check a file in the directory tree

		// if we are running restricted, the only files we
		// will allow to come from the directory are .cfg files
		len = strlen(filename);
		// FIXME TTimo I'm not sure about the fs_numServerPaks test
		// if you are using FS_ReadFile to find out if a file exists,
		//   this test can make the search fail although the file is in the directory
		// I had the problem on https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=8
		// turned out I used FS_FileExists instead
		if( fs_numServerPaks)
		{
			if(!FS_IsExt(filename, ".cfg", len) &&		// for config files
			   !FS_IsExt(filename, ".menu", len) &&		// menu files
			   !FS_IsExt(filename, ".game", len) &&		// menu files
			   !FS_IsExt(filename, ".dat", len) &&		// for journal files
			   !FS_IsDemoExt(filename, len))			// demos
			{
				h = 0;
                len = -1;
		        continue;
			}
		}

		directory_t	* dir = search->dir;

		char* netpath = FS_BuildOSPath(dir->path, dir->gamedir, filename);
		FILE* filep = Sys_FOpen(netpath, "rb");

		if (filep == NULL)
		{
			h = 0;
            len = -1;
		    continue;
		}

		Q_strncpyz(fsh[h].name, filename, sizeof(fsh[h].name));
		fsh[h].zipFile = qfalse;

		if(fs_debug->integer)
		{
			Com_Printf("FS_FOpenFileRead: %s (found in '%s%c%s')\n", filename, dir->path, PATH_SEP, dir->gamedir);
		}

		fsh[h].handleFiles.file.o = filep;
		len = FS_fplength(filep);
		if(len >= 0 && h)
			break;

	}

	h = 0;
	len = -1;

_PROC_END_:

///////////////////////////////////////////////////////////////////////////
		if(len >= 0 && h)
			break;
	}
	

	if( h == 0 )
    {
		if ( buffer )
			*buffer = NULL;
		return -1;
	}

	if ( buffer == 0 )
    {
		FS_FCloseFile(h);
		return len;
	}

	fs_loadCount++;
	fs_loadStack++;

	char* buf = Hunk_AllocateTempMemory(len+1);
	*buffer = buf;
    FS_Read(buf, len, h);

	// guarantee that it will have a trailing 0 for string operations
	buf[len] = 0;
	FS_FCloseFile( h );


	return len;
}



static void vk_stagBufferToDeviceLocalMem(VkImage image, VkBufferImageCopy* pRegion, uint32_t num_region)
{
    // An application can copy buffer and image data using several methods
    // depending on the type of data transfer. Data can be copied between
    // buffer objects with vkCmdCopyBuffer and a portion of an image can 
    // be copied to another image with vkCmdCopyImage. 
    //
    // Image data can also be copied to and from buffer memory using
    // vkCmdCopyImageToBuffer and vkCmdCopyBufferToImage.
    //
    // Image data can be blitted (with or without scaling and filtering) 
    // with vkCmdBlitImage. Multisampled images can be resolved to a 
    // non-multisampled image with vkCmdResolveImage.
    /*
    VkCommandBuffer HCmdBuf;

    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.commandPool = vk.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VK_CHECK( qvkAllocateCommandBuffers(vk.device, &alloc_info, &HCmdBuf) );

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;
    VK_CHECK( qvkBeginCommandBuffer(HCmdBuf, &begin_info) );
*/

    vk_beginRecordCmds( vk.tmpRecordBuffer );
/*
    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = NULL;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = StagBuf.buff;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    NO_CHECK( qvkCmdPipelineBarrier( vk.tmpRecordBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL) );
*/
    record_image_layout_transition( vk.tmpRecordBuffer, image, 
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            VK_IMAGE_LAYOUT_UNDEFINED, 
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);


    // To copy data from a buffer object to an image object

    // HCmdBuf is the command buffer into which the command will be recorded.
    // StagBuf.buff is the source buffer.
    // image is the destination image.
    // dstImageLayout is the layout of the destination image subresources.
    // curLevel is the number of regions to copy.
    // pRegions is a pointer to an array of VkBufferImageCopy structures
    // specifying the regions to copy.
    NO_CHECK( qvkCmdCopyBufferToImage( vk.tmpRecordBuffer, StagBuf.buff, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, num_region, pRegion) );

    record_image_layout_transition(vk.tmpRecordBuffer, image,
            VK_IMAGE_ASPECT_COLOR_BIT, 
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


    vk_commitRecordedCmds(vk.tmpRecordBuffer);
}

/*
	for(stage = 0; (stage < MAX_SHADER_STAGES) && (NULL != pTess->xstages[stage]); ++stage )
	{

		ComputeColors( pTess->xstages[stage] );
		ComputeTexCoords( pTess->xstages[stage] );

        // base
        // set state
		//R_BindAnimatedImage( &tess.xstages[stage]->bundle[0] );
        VkBool32 multitexture = (tess.xstages[stage]->bundle[1].image[0] != NULL);

    {        
	    if ( tess.xstages[stage]->bundle[0].isVideoMap )
        {
		    ri.CIN_RunCinematic(tess.xstages[stage]->bundle[0].videoMapHandle);
		    ri.CIN_UploadCinematic(tess.xstages[stage]->bundle[0].videoMapHandle);
		    goto ENDANIMA;
	    }

        int numAnimaImg = tess.xstages[stage]->bundle[0].numImageAnimations;

        if ( numAnimaImg <= 1 )
        {
		    updateCurDescriptor( tess.xstages[stage]->bundle[0].image[0]->descriptor_set, 0);
            //GL_Bind(tess.xstages[stage]->bundle[0].image[0]);
            goto ENDANIMA;
	    }

        // it is necessary to do this messy calc to make sure animations line up
        // exactly with waveforms of the same frequency
	    int index = (int)( tess.shaderTime * tess.xstages[stage]->bundle[0].imageAnimationSpeed * FUNCTABLE_SIZE ) >> FUNCTABLE_SIZE2;
        
        if ( index < 0 ) {
		    index = 0;	// may happen with shader time offsets
	    }

	    index %= numAnimaImg;

	    updateCurDescriptor( tess.xstages[stage]->bundle[0].image[ index ]->descriptor_set, 0);
        //GL_Bind(tess.xstages[stage]->bundle[0].image[ index ]);
    }
    
ENDANIMA:
		//
		// do multitexture
		//

		if ( multitexture )
		{
            // DrawMultitextured( input, stage );
            // output = t0 * t1 or t0 + t1

            // t0 = most upstream according to spec
            // t1 = most downstream according to spec
            // this is an ugly hack to work around a GeForce driver
            // bug with multitexture and clip planes


            if ( tess.xstages[stage]->bundle[1].isVideoMap )
            {
                ri.CIN_RunCinematic(tess.xstages[stage]->bundle[1].videoMapHandle);
                ri.CIN_UploadCinematic(tess.xstages[stage]->bundle[1].videoMapHandle);
                goto END_ANIMA2;
            }

            if ( tess.xstages[stage]->bundle[1].numImageAnimations <= 1 ) {
                updateCurDescriptor( tess.xstages[stage]->bundle[1].image[0]->descriptor_set, 1);
                goto END_ANIMA2;
            }

            // it is necessary to do this messy calc to make sure animations line up
            // exactly with waveforms of the same frequency
            int index2 = (int)( tess.shaderTime * tess.xstages[stage]->bundle[1].imageAnimationSpeed * FUNCTABLE_SIZE ) >> FUNCTABLE_SIZE2;

            if ( index2 < 0 ) {
                index2 = 0;	// may happen with shader time offsets
            }
	        
            index2 %= tess.xstages[stage]->bundle[1].numImageAnimations;

            updateCurDescriptor( tess.xstages[stage]->bundle[1].image[ index2 ]->descriptor_set , 1);

END_ANIMA2:

            if (r_lightmap->integer)
                updateCurDescriptor(tr.whiteImage->descriptor_set, 0); 
            
            // replace diffuse texture with a white one thus effectively render only lightmap
		}
*/


/*
static unsigned int NameToAFunc( const char *funcname )
{	
	if ( isNonCaseStringEqual( funcname, "GT0" ) )
	{
		return GLS_ATEST_GT_0;
	}
	else if ( isNonCaseStringEqual( funcname, "LT128" ) )
	{
		return GLS_ATEST_LT_80;
	}
	else if ( isNonCaseStringEqual( funcname, "GE128" ) )
	{
		return GLS_ATEST_GE_80;
	}

	ri.Printf( PRINT_WARNING, "WARNING: invalid alphaFunc name '%s' in shader '%s'\n",
            funcname, shader.name );
	return 0;
}
*/

/*
void R_TakeScreenshotCmd( int x, int y, int width, int height, char *name, qboolean jpeg )
{
	static char	fileName[MAX_OSPATH] = {0}; // bad things if two screenshots per frame?
	
    screenshotCommand_t	*cmd = (screenshotCommand_t*) R_GetCommandBuffer(sizeof(*cmd));
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SCREENSHOT;

	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	
    //Q_strncpyz( fileName, name, sizeof(fileName) );

    strncpy(fileName, name, sizeof(fileName));

	cmd->fileName = fileName;
	cmd->jpeg = jpeg;
}

*/


    ////
    ri.Printf(PRINT_ALL, " Create Q3 stencil shadows volume pipelines. \n");
    
    vk_create_pipeline( 0, 
            ST_SINGLE_TEXTURE, CT_FRONT_SIDED, SHADOWS_RENDERING_EDGES, 
            VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE,
            &g_globalPipelines.shadow_volume_pipelines[0][0] );

    vk_create_pipeline( 0, 
            ST_SINGLE_TEXTURE, CT_FRONT_SIDED, SHADOWS_RENDERING_EDGES, 
            VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE,
            &g_globalPipelines.shadow_volume_pipelines[0][1] );

    vk_create_pipeline( 0, 
            ST_SINGLE_TEXTURE, CT_BACK_SIDED, SHADOWS_RENDERING_EDGES, 
            VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE,
            &g_globalPipelines.shadow_volume_pipelines[1][0] );

    vk_create_pipeline( 0, 
            ST_SINGLE_TEXTURE, CT_BACK_SIDED, SHADOWS_RENDERING_EDGES, 
            VK_FALSE, VK_TRUE, VK_FALSE, VK_FALSE,
            &g_globalPipelines.shadow_volume_pipelines[1][1] );


    ////
    ri.Printf(PRINT_ALL, " Create Q3 stencil shadows finish pipeline. \n");

    vk_create_pipeline( 
            GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO, 
            ST_SINGLE_TEXTURE, 
            CT_FRONT_SIDED,
            SHADOWS_RENDERING_FULLSCREEN_QUAD, 
            VK_FALSE, 
            VK_FALSE, 
            VK_FALSE,
            VK_FALSE,
            &g_globalPipelines.shadow_finish_pipeline );


/////////////////////////////////////////////////////////////////

static void vk_stagBufferToDeviceLocalMem(VkImage hImage, VkBuffer hBuffer, 
        VkBufferImageCopy* const pRegion, const uint32_t nRegion)
{
//  One thing that is fundamental to the operation of vulkan is that
//  the commands are not executed as soon as they are called. Rather,
//  they are simply added to the end of the specified command buffer.
//  If you are copying data to or from a region of memory that is 
//  visible to the host(i.e., it's mapped), then you need to be sure
//  of several things:
//
//  1) Ensure that the data is in the source region before the command
//  is executed by the device.
//
//  2) Ensure that the data in the source region is valid until after
//  the commmand has been executed on the device.
//
//  3) Ensure that you don't read the destination data until after
//  the command has been executed on the device.
//  
    vk_beginRecordCmds( vk.tmpRecordBuffer );

    // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : The image is the
    // destination of copy operations. 
    //
    // A barrier is a synchronization mechanism for memory access manangement
    // and resource state movement within the stages of vulkan pipeline.
    // The primary command for synchronizing access to resources and moving
    // them from state to state.
    // 
    // vkCmdPipelineBarrier is a synchronization command that inserts
    // a dependency between commands submitted to the same queue, or 
    // between commands in the same subpass. When vkCmdPipelineBarrier
    // is submitted to a queue, it defines a memory dependency between
    // commands that were submitted before it, and those submitted 
    // after it.
    //
    // srcStageMask and dstStageMask specify which pipeline stages wrote
    // to the resource last and which stages will read from the resource
    // next, respectively. That is, they specify the source and destination
    // for data flow represented by the barrier.

    // VK_PIPELINE_STAGE_ALL_COMMANDS_BIT: This stage is the big hammer.
    // whenever you just don't know what;s is going on, use this; it will
    // synchronize everything with everything. Just use it wisely.
    // cmdBuf is the command buffer into which the command is recorded.
	
    VkImageMemoryBarrier barrier ;
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.pNext = NULL;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = hImage;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	
    NO_CHECK( qvkCmdPipelineBarrier(vk.tmpRecordBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,	0, NULL, 0, NULL, 1, &barrier) );


    // To copy data from a buffer object to an image object
    // hBuffer is the source buffer, hImage is the destination image.
    // dstImageLayout is the layout of the destination image subresources.
    // curLevel is the number of regions to copy.
    // pRegions is a pointer to an array of VkBufferImageCopy structures
    // specifying the regions to copy.
    NO_CHECK( qvkCmdCopyBufferToImage( vk.tmpRecordBuffer, hBuffer, hImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, nRegion, pRegion) );

    VkImageMemoryBarrier barrier2 ;
	barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier2.pNext = NULL;
	barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier2.image = hImage;
	barrier2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier2.subresourceRange.baseMipLevel = 0;
	barrier2.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	barrier2.subresourceRange.baseArrayLayer = 0;
	barrier2.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	
    NO_CHECK( qvkCmdPipelineBarrier(vk.tmpRecordBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier2) );


    vk_commitRecordedCmds(vk.tmpRecordBuffer);
}


