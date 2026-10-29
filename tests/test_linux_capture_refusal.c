#include "media/media.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* No SDL library is linked. Any accidentally reached backend call fails. */
SDL_CameraID *SDLCALL SDL_GetCameras(int *count) { (void)count; fputs("FORBIDDEN SDL_GetCameras\n",stderr); abort(); }
void SDLCALL SDL_free(void *ptr) { (void)ptr; fputs("FORBIDDEN SDL_free\n",stderr); abort(); }
int main(void) {
 unsigned checks=0; const int owners[3]={1,2,3};
 assert(cam_permission()==-2);checks++;assert(mic_permission()==-2);checks++;
 assert(cam_count()==0);checks++;assert(!mic_permission_request());checks++;
 for(unsigned i=0;i<3;i++){
  assert(cam_open(&owners[i],640,480,30,0)==-2);checks++;
  assert(cam_open(&owners[i],0,0,0,2)==-2);checks++;
  assert(mic_open(&owners[i],false)==-2);checks++;
  assert(mic_open(&owners[i],true)==-2);checks++;
 }
 assert(cam_count()==0);checks++;assert(!mic_active());checks++;
 assert(cam_timeout_ms()==-1);checks++;
 printf("production Linux capture refusal: %u checks; no SDL backend linked or called\n",checks);return 0;
}
