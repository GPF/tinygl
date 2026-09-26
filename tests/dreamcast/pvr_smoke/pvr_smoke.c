#include <kos.h>
#include <GL/gl.h>

#define FRAME_LIMIT 600

int main(int argc, char **argv) {
    int frame;

    (void)argc;
    (void)argv;

    printf("pvr_smoke: setting 640x480 RGB565 video mode\n");
    vid_set_mode(DM_640x480, PM_RGB565);

    if(glInitPVR(640, 480) < 0) {
        printf("pvr_smoke: TinyGL PVR initialization failed\n");
        return 1;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glColor3f(1.0f, 0.0f, 0.0f);

    printf("pvr_smoke: drawing TinyGL red triangle for %d frames\n", FRAME_LIMIT);
    for(frame = 0; frame < FRAME_LIMIT; ++frame) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glVertex2f(-0.5f, -0.5f);
        glVertex2f( 0.5f, -0.5f);
        glVertex2f( 0.0f,  0.5f);
        glEnd();
        glFlush();

        if(frame == 0 || (frame + 1) % 120 == 0) {
            printf("pvr_smoke: frame %d\n", frame + 1);
        }
    }

    glClose();
    printf("pvr_smoke: PASS\n");
    return 0;
}
