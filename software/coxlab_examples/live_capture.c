/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2010 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "libfreenect.h"

#include <pthread.h>

#if defined(__APPLE__)
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/glut.h>
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <sys/stat.h>

#include <math.h>

pthread_t freenect_thread;
volatile int die = 0;

int g_argc;
char **g_argv;

int window;


// TODO!!!
#define GPIO_FILENAME "/Users/graham/tmp/gpio1"
#define DATA_DIR "/Users/graham/tmp/kinect_data"

char data_dir[1024];

typedef int bool;
FILE *gpio_file;

bool acquire_flag = 0;
#define MIN_FRAMES_BETWEEN_ACQ   10
int acquire_countdown = 0;
int acquired_frame_number = 0;
bool acquired_rgb = 0;
bool acquired_depth = 0;

#define RGB_FILE_STEM "rgb_"
#define DEPTH_FILE_STEM "depth_"

pthread_mutex_t acquire_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gl_backbuf_mutex = PTHREAD_MUTEX_INITIALIZER;

// back: owned by libfreenect (implicit for depth)
// mid: owned by callbacks, "latest frame ready"
// front: owned by GL, "currently being drawn"
uint16_t *depth_back, *depth_mid, *depth_front;
uint8_t *rgb_back, *rgb_mid, *rgb_front;

GLuint gl_depth_tex;
GLuint gl_rgb_tex;

freenect_context *f_ctx;
freenect_device *f_dev;
int freenect_angle = -30;
int freenect_led;

freenect_video_format requested_format = FREENECT_VIDEO_RGB;
freenect_video_format current_format = FREENECT_VIDEO_RGB;

pthread_cond_t gl_frame_cond = PTHREAD_COND_INITIALIZER;
int got_rgb = 0;
int got_depth = 0;



void DrawGLScene()
{
	pthread_mutex_lock(&gl_backbuf_mutex);

	while (!got_depth || !got_rgb) {
		pthread_cond_wait(&gl_frame_cond, &gl_backbuf_mutex);
	}

	if (requested_format != current_format) {
		pthread_mutex_unlock(&gl_backbuf_mutex);
		return;
	}

	void *tmp;

	if (got_depth) {
		tmp = depth_front;
		depth_front = depth_mid;
		depth_mid = tmp;
		got_depth = 0;
	}
	if (got_rgb) {
		tmp = rgb_front;
		rgb_front = rgb_mid;
		rgb_mid = tmp;
		got_rgb = 0;
	}

	pthread_mutex_unlock(&gl_backbuf_mutex);

	glClear(GL_COLOR_BUFFER_BIT);// | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glEnable(GL_TEXTURE_2D);

	glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
    //glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelTransferf(GL_RED_BIAS, 1.0);
    glPixelTransferf(GL_GREEN_BIAS, 2.0);
    glPixelTransferf(GL_BLUE_BIAS, 3.0);
    glPixelTransferf(GL_RED_SCALE, -96.);
    glPixelTransferf(GL_GREEN_SCALE, -96.);
    glPixelTransferf(GL_BLUE_SCALE, -96.);
	//glTexImage2D(GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, depth_front);
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE16, 640, 480, 0, GL_LUMINANCE, GL_UNSIGNED_SHORT, depth_front);
    //glTexImage2D(GL_TEXTURE_2D, 0, 1, 640, 480, 0, GL_LUMINANCE, GL_UNSIGNED_SHORT, depth_front);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 640, 480, 0, GL_LUMINANCE, GL_UNSIGNED_SHORT, depth_front);

	glBegin(GL_TRIANGLE_FAN);
	//glColor4f(255.0f, 255.0f, 255.0f, 255.0f);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0,0,0);
	glTexCoord2f(1, 0); glVertex3f(640,0,0);
	glTexCoord2f(1, 1); glVertex3f(640,480,0);
	glTexCoord2f(0, 1); glVertex3f(0,480,0);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
    glPixelTransferf(GL_RED_BIAS, 0.);
    glPixelTransferf(GL_GREEN_BIAS, 0.);
    glPixelTransferf(GL_BLUE_BIAS, 0.);
    glPixelTransferf(GL_RED_SCALE, 1.);
    glPixelTransferf(GL_GREEN_SCALE, 1.);
    glPixelTransferf(GL_BLUE_SCALE, 1.);
	if (current_format == FREENECT_VIDEO_RGB)
		glTexImage2D(GL_TEXTURE_2D, 0, 3, 640, 480, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_front);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, 1, 640, 480, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, rgb_front+640*4);

	glBegin(GL_TRIANGLE_FAN);
	//glColor4f(255.0f, 255.0f, 255.0f, 255.0f);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(640,0,0);
	glTexCoord2f(1, 0); glVertex3f(1280,0,0);
	glTexCoord2f(1, 1); glVertex3f(1280,480,0);
	glTexCoord2f(0, 1); glVertex3f(640,480,0);
	glEnd();

	//glutSwapBuffers();
    glFlush();
}



bool initGPIO(){
  gpio_file = fopen(GPIO_FILENAME, "r");
  return 1;
}

bool closeGPIO(){
  
  fclose(gpio_file);
  return 1;
}

bool pollGPIO(){

  char result;
  
  initGPIO();
  fseek(gpio_file, 0, 0);
  fread(&result, sizeof(char), 1, gpio_file);
  closeGPIO();
  
  if(result == '0'){
    return 0;
  } else {
    return 1;
  }
  
}

void acquireFrame(){
  
  printf("Acquiring frame %d\n", acquired_frame_number++);
  acquire_countdown = MIN_FRAMES_BETWEEN_ACQ;
  
  pthread_mutex_lock(&acquire_mutex);
  acquired_rgb = 0;
  acquired_depth = 0;
	pthread_mutex_unlock(&acquire_mutex);
}


void saveRGBFrame(uint8_t *frame){
  char filename[512];
  sprintf(filename, "%s/%s%d.dat", data_dir, RGB_FILE_STEM, acquired_frame_number);
  
  printf("Saving %s\n", filename);
  
  FILE *rgb_file = fopen(filename, "w");
  fwrite((void *)frame, 1, 640*480*3, rgb_file);
  fclose(rgb_file);
  
  pthread_mutex_lock(&acquire_mutex);
  acquired_rgb = 1;
	pthread_mutex_unlock(&acquire_mutex);
}

void saveDepthFrame(uint16_t *frame){
  char filename[512];
  sprintf(filename, "%s/%s%d.dat", data_dir, DEPTH_FILE_STEM, acquired_frame_number);
  
  printf("Saving %s\n", filename);  
  
  FILE *depth_file = fopen(filename, "w");
  fwrite((void *)frame, 1, 640*480*2, depth_file);
  fclose(depth_file);
  
  pthread_mutex_lock(&acquire_mutex);
  acquired_depth = 1;
	pthread_mutex_unlock(&acquire_mutex);
}


void Idle(){

  bool trigger = 1;
  
  
  if(acquire_countdown <= 0){
    trigger = pollGPIO();
  }
  
  if(!trigger){ // gpio is a pull-up so look for 0
    acquireFrame();
  }
  
  fflush(stdout);
  DrawGLScene();
}


void keyPressed(unsigned char key, int x, int y)
{
	if (key == 27) {
		die = 1;
		pthread_join(freenect_thread, NULL);
		glutDestroyWindow(window);
        free(depth_back);
		free(depth_mid);
		free(depth_front);
		free(rgb_back);
		free(rgb_mid);
		free(rgb_front);
		pthread_exit(NULL);
	}
    // if (key == 'w') {
    //  freenect_angle++;
    //  if (freenect_angle > 30) {
    //      freenect_angle = 30;
    //  }
    // }
    // if (key == 's') {
    //  freenect_angle = 0;
    // }
	if (key == 'f') {
		if (requested_format == FREENECT_VIDEO_IR_8BIT)
			requested_format = FREENECT_VIDEO_RGB;
		else
			requested_format = FREENECT_VIDEO_IR_8BIT;
	}
    // if (key == 'x') {
    //  freenect_angle--;
    //  if (freenect_angle < -30) {
    //      freenect_angle = -30;
    //  }
    // }
	if (key == '1') {
		freenect_set_led(f_dev,LED_GREEN);
	}
	if (key == '2') {
		freenect_set_led(f_dev,LED_RED);
	}
	if (key == '3') {
		freenect_set_led(f_dev,LED_YELLOW);
	}
	if (key == '4') {
		freenect_set_led(f_dev,LED_BLINK_YELLOW);
	}
	if (key == '5') {
		freenect_set_led(f_dev,LED_BLINK_GREEN);
	}
	if (key == '6') {
		freenect_set_led(f_dev,LED_BLINK_RED_YELLOW);
	}
	if (key == '0') {
		freenect_set_led(f_dev,LED_OFF);
	}
	//freenect_set_tilt_degs(f_dev,freenect_angle);
}

void ReSizeGLScene(int Width, int Height)
{
	glViewport(0,0,Width,Height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho (0, 1280, 480, 0, -1.0f, 1.0f);
	glMatrixMode(GL_MODELVIEW);
}

void InitGL(int Width, int Height)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	//glClearDepth(1.0);
	//glDepthFunc(GL_LESS);
	//glDisable(GL_DEPTH_TEST);
	//glEnable(GL_BLEND);
	//glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glShadeModel(GL_SMOOTH);
	glGenTextures(1, &gl_depth_tex);
	glBindTexture(GL_TEXTURE_2D, gl_depth_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenTextures(1, &gl_rgb_tex);
	glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	ReSizeGLScene(Width, Height);
}

void *gl_threadfunc(void *arg)
{
	printf("GL thread\n");

	glutInit(&g_argc, g_argv);

	//glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
    glutInitDisplayMode(GLUT_RGBA | GLUT_SINGLE);// | GLUT_ALPHA | GLUT_DEPTH);
	glutInitWindowSize(1280, 480);
	glutInitWindowPosition(0, 0);

	window = glutCreateWindow("LibFreenect");

	glutDisplayFunc(&DrawGLScene);
	glutIdleFunc(&Idle);
	glutReshapeFunc(&ReSizeGLScene);
	glutKeyboardFunc(&keyPressed);

	InitGL(1280, 480);

	glutMainLoop();

	return NULL;
}

//uint16_t t_gamma[2048];

void depth_cb(freenect_device *dev, void *v_depth, uint32_t timestamp)
{
    bool local_acquire_flag = 0;
    pthread_mutex_lock(&acquire_mutex);
    local_acquire_flag = !acquired_depth;
	pthread_mutex_unlock(&acquire_mutex);

	pthread_mutex_lock(&gl_backbuf_mutex);
    
    assert(depth_back == v_depth);
    depth_back = depth_mid;
    freenect_set_depth_buffer(dev, depth_back);
    
    if (local_acquire_flag){
        saveDepthFrame(depth_back);
    }
    
    depth_mid = v_depth;
    
	got_depth++;
	pthread_cond_signal(&gl_frame_cond);
	pthread_mutex_unlock(&gl_backbuf_mutex);
}

void rgb_cb(freenect_device *dev, void *rgb, uint32_t timestamp)
{
  bool local_acquire_flag = 0;
  	
  pthread_mutex_lock(&acquire_mutex);
  local_acquire_flag = !acquired_rgb;
	pthread_mutex_unlock(&acquire_mutex);
  
  pthread_mutex_lock(&gl_backbuf_mutex);

	// swap buffers
	assert (rgb_back == rgb);
	rgb_back = rgb_mid;
	freenect_set_video_buffer(dev, rgb_back);
	
	if(local_acquire_flag){
    saveRGBFrame(rgb_back);
	}
	
	rgb_mid = rgb;

	got_rgb++;
	pthread_cond_signal(&gl_frame_cond);
	pthread_mutex_unlock(&gl_backbuf_mutex);
	
	if(acquire_countdown){
    acquire_countdown--;
	}
}

void *freenect_threadfunc(void *arg)
{
	//freenect_set_tilt_degs(f_dev,freenect_angle);
	freenect_set_led(f_dev,LED_RED);
	freenect_set_depth_callback(f_dev, depth_cb);
	freenect_set_video_callback(f_dev, rgb_cb);
	freenect_set_video_format(f_dev, current_format);
	freenect_set_depth_format(f_dev, FREENECT_DEPTH_11BIT);
	freenect_set_video_buffer(f_dev, rgb_back);
    freenect_set_depth_buffer(f_dev, depth_back);
    
	freenect_start_depth(f_dev);
	freenect_start_video(f_dev);

	//printf("'w'-tilt up, 's'-level, 'x'-tilt down, '0'-'6'-select LED mode, 'f'-video format\n");

	while (!die && freenect_process_events(f_ctx) >= 0) {
		freenect_raw_tilt_state* state;
		freenect_update_tilt_state(f_dev);
		state = freenect_get_tilt_state(f_dev);
		//double dx,dy,dz;
		//freenect_get_mks_accel(state, &dx, &dy, &dz);
		//printf("\r raw acceleration: %4d %4d %4d  mks acceleration: %4f %4f %4f", state->accelerometer_x, state->accelerometer_y, state->accelerometer_z, dx, dy, dz);
		//fflush(stdout);

		if (requested_format != current_format) {
			freenect_stop_video(f_dev);
			freenect_set_video_format(f_dev, requested_format);
			freenect_start_video(f_dev);
			current_format = requested_format;
		}
	}

	printf("\nshutting down streams...\n");

	freenect_stop_depth(f_dev);
	freenect_stop_video(f_dev);

	freenect_close_device(f_dev);
	freenect_shutdown(f_ctx);

	printf("-- done!\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int res;

  //initGPIO();
  bool pin = pollGPIO();
  printf("GPIO pin = %d\n", pin);

  int data_dir_number = 0;
  sprintf(data_dir, "%s/%d", DATA_DIR, data_dir_number);
  struct stat st;
  while(stat(data_dir,&st) == 0){
    data_dir_number++;
    sprintf(data_dir, "%s/%d", DATA_DIR, data_dir_number);
  }
  
  mkdir(data_dir, S_IREAD | S_IWRITE | S_IEXEC);
  
  printf("Data dir is: %s\n", data_dir);

  depth_back = malloc(640*480*2);
	depth_mid = malloc(640*480*2);
	depth_front = malloc(640*480*2);
	rgb_back = malloc(640*480*3);
	rgb_mid = malloc(640*480*3);
	rgb_front = malloc(640*480*3);

	printf("Kinect camera test\n");

	//int i;
	//for (i=0; i<2048; i++) {
	//	float v = i/2048.0;
	//	v = powf(v, 3)* 6;
	//	t_gamma[i] = v*6*256;
	//}

	g_argc = argc;
	g_argv = argv;

	if (freenect_init(&f_ctx, NULL) < 0) {
		printf("freenect_init() failed\n");
		return 1;
	}

	freenect_set_log_level(f_ctx, FREENECT_LOG_FATAL);

	int nr_devices = freenect_num_devices (f_ctx);
	printf ("Number of devices found: %d\n", nr_devices);

	int user_device_number = 0;
	if (argc > 1)
		user_device_number = atoi(argv[1]);

    if (nr_devices < 1)
		return 1;

	if (freenect_open_device(f_ctx, &f_dev, user_device_number) < 0) {
		printf("Could not open device\n");
		return 1;
	}

	res = pthread_create(&freenect_thread, NULL, freenect_threadfunc, NULL);
	if (res) {
		printf("pthread_create failed\n");
		return 1;
	}
	
	// OS X requires GLUT to run on the main thread
	gl_threadfunc(NULL);
	
  //closeGPIO();

	return 0;
}
