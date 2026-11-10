#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "MTLFXScaler.h"
#include <cstdio>
#include <vector>
#include <utility>
#include <stdexcept>
static void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
 @autoreleasepool { try {
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  Check(Metal::FXSpatialScaler::Supported(device), "MetalFX not available on this test GPU");
  id<MTLCommandQueue> queue = [device newCommandQueue];
  auto descriptor = [MTLTextureDescriptor new];
  descriptor.textureType = MTLTextureType2DArray; descriptor.arrayLength = 1;
  descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm; descriptor.width = 96; descriptor.height = 96;
  descriptor.storageMode = MTLStorageModeShared; descriptor.usage = MTLTextureUsageShaderRead;
  id<MTLTexture> source = [device newTextureWithDescriptor:descriptor];
  Check(source != nil, "No source texture");
  std::vector<unsigned char> pixels(96*96*4,0);
  for (int y=0;y<96;++y) for(int x=0;x<96;++x) {
    auto* p=&pixels[(y*96+x)*4];
    p[0]=(x<16||x>=80||y<16||y>=80)?255:0; p[2]=p[0]?0:255; p[3]=255;
  }
  [source replaceRegion:MTLRegionMake2D(0,0,96,96) mipmapLevel:0 slice:0
                withBytes:pixels.data() bytesPerRow:96*4 bytesPerImage:96*96*4];
  Metal::FXSpatialScaler scaler;
  id<MTLTexture> previous=nil;
  for (const auto [width,height] : {std::pair{128u,128u},std::pair{128u,128u},std::pair{192u,128u},std::pair{256u,128u}}) {
    id<MTLCommandBuffer> command = [queue commandBuffer];
    auto output = scaler.Encode(device,command,source,16,16,64,64,width,height);
    Check(output && output.width==width && output.height==height && output.textureType==MTLTextureType2DArray,
          "Wrong output texture or array view");
    if(width==128 && height==128 && previous) Check(output==previous,"Scaler resources were not reused");
    const unsigned row=(width*4+255)&~255u;
    id<MTLBuffer> buffer=[device newBufferWithLength:row*height options:MTLResourceStorageModeShared];
    auto blit=[command blitCommandEncoder];
    [blit copyFromTexture:output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
               sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
       destinationBytesPerRow:row destinationBytesPerImage:row*height];
    [blit endEncoding]; [command commit]; [command waitUntilCompleted];
    Check(command.status==MTLCommandBufferStatusCompleted, "GPU encoding failed");
    auto* data=static_cast<unsigned char*>(buffer.contents);
    for(unsigned y : {4u,height/2,height-5}) for(unsigned x : {4u,width/2,width-5}) {
      auto* p=data+y*row+x*4;
      Check(p[0]<10 && p[1]<10 && p[2]>245 && p[3]>245,"Upscale did not preserve the cropped blue image");
    }
    previous=output; [buffer release];
  }
  auto command=[queue commandBuffer];
  Check(!scaler.Encode(device,command,source,95,0,64,64,128,128),"Out-of-bounds crop accepted");
  Check(!scaler.Encode(device,command,source,0,0,64,64,32,32),"Downscale accepted");
  scaler.Reset();
  [source release]; [descriptor release]; [queue release]; [device release];
  std::puts("PASS: MetalFX GPU output, cropped source, 2D-array sampling view, cache reuse, resize, invalid bounds and downscale fallback");
  return 0;
 } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; } }
}
