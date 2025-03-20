#include <stdio.h>
#include <algorithm>
#include <getopt.h>

#include "CycleTimer.h"

extern void mandelbrotSerial(
    float x0, float y0, float x1, float y1,
    int width, int height,
    int startRow, int numRows,
    int maxIterations,
    int output[]);

extern void mandelbrotThread(
    int numThreads,
    float x0, float y0, float x1, float y1,
    int width, int height,
    int maxIterations,
    int output[]);

extern void writePPMImage(
    int* data,
    int width, int height,
    const char *filename,
    int maxIterations);

void
scaleAndShift(float& x0, float& x1, float& y0, float& y1,
              float scale,
              float shiftX, float shiftY)
{

    x0 *= scale;
    x1 *= scale;
    y0 *= scale;
    y1 *= scale;
    x0 += shiftX;
    x1 += shiftX;
    y0 += shiftY;
    y1 += shiftY;

}

void usage(const char* progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Program Options:\n");
    printf("  -t  --threads <N>  Use N threads\n");
    printf("  -v  --view <INT>   Use specified view settings\n");
    printf("  -?  --help         This message\n");
}

bool verifyResult (int *gold, int *result, int width, int height) {

    int i, j;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            if (gold[i * width + j] != result[i * width + j]) {
                printf ("Mismatch : [%d][%d], Expected : %d, Actual : %d\n",
                            i, j, gold[i * width + j], result[i * width + j]);
                return 0;
            }
        }
    }

    return 1;
}

/*
    简单按行分割：
    numThreads = 1:     [592.929] ms
    numThreads = 2:     [299.835] ms    (1.99x speedup from 2 threads)
    numThreads = 3:     [359.113] ms    (1.65x speedup from 3 threads)
    numThreads = 4:     [240.334] ms    (2.44x speedup from 4 threads)
    numThreads = 5:     [235.651] ms    (2.51x speedup from 5 threads)
    numThreads = 6:     [179.616] ms    (3.32x speedup from 6 threads)
    numThreads = 7:     [170.607] ms    (3.44x speedup from 7 threads)
    numThreads = 8:     [144.491] ms    (4.05x speedup from 8 threads)
    numThreads = 16:    [96.739] ms     (6.06x speedup from 16 threads)
    numThreads = 32:    [84.951] ms     (6.90x speedup from 32 threads)

    模运算分割：
    numThreads = 2:     [295.901] ms    (2.00x speedup from 2 threads)
    numThreads = 3:     [196.242] ms    (3.02x speedup from 3 threads)
    numThreads = 4:     [147.119] ms    (4.05x speedup from 4 threads)
    numThreads = 5:     [126.715] ms    (4.65x speedup from 5 threads)
    numThreads = 6:     [105.913] ms    (5.59x speedup from 6 threads)
    numThreads = 7:     [91.051] ms     (6.49x speedup from 7 threads)
    numThreads = 8:     [81.476] ms     (7.28x speedup from 8 threads)
    numThreads = 16:    [84.275] ms     (7.01x speedup from 16 threads)

    原因分析：
    可以合理分配任务，使每个线程计算的任务量类似，避免出现任务积压在单一线程
    自己的思考：也有可能使用模运算分割可以减少存储器访问次数，可以理解为一段时间内各线程需要的数据块为同一数据块，
    而简单按行分割则可能需要在线程切换的同时，因为所需数据块距离较远，cache或内存未命中而切换访问的数据块，
    至于16线程表现平平乃至更差，是因为机器最高只能支持八线程，导致大量时间浪费在线程切换上

*/


int main(int argc, char** argv) {

    const unsigned int width = 1600;
    const unsigned int height = 1200;
    const int maxIterations = 256;
    int numThreads = 8;

    float x0 = -2;
    float x1 = 1;
    float y0 = -1;
    float y1 = 1;

    // parse commandline options ////////////////////////////////////////////
    int opt;
    static struct option long_options[] = {
        {"threads", 1, 0, 't'},
        {"view", 1, 0, 'v'},
        {"help", 0, 0, '?'},
        {0 ,0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "t:v:?", long_options, NULL)) != EOF) {

        switch (opt) {
        case 't':
        {
            numThreads = atoi(optarg);
            break;
        }
        case 'v':
        {
            int viewIndex = atoi(optarg);
            // change view settings
            if (viewIndex == 2) {
                float scaleValue = .015f;
                float shiftX = -.986f;
                float shiftY = .30f;
                scaleAndShift(x0, x1, y0, y1, scaleValue, shiftX, shiftY);
            } else if (viewIndex > 1) {
                fprintf(stderr, "Invalid view index\n");
                return 1;
            }
            break;
        }
        case '?':
        default:
            usage(argv[0]);
            return 1;
        }
    }
    // end parsing of commandline options


    int* output_serial = new int[width*height];
    int* output_thread = new int[width*height];
    
    //
    // Run the serial implementation.  Run the code three times and
    // take the minimum to get a good estimate.
    //

    double minSerial = 1e30;
    for (int i = 0; i < 5; ++i) {
       memset(output_serial, 0, width * height * sizeof(int));
        double startTime = CycleTimer::currentSeconds();
        mandelbrotSerial(x0, y0, x1, y1, width, height, 0, height, maxIterations, output_serial);
        double endTime = CycleTimer::currentSeconds();
        minSerial = std::min(minSerial, endTime - startTime);
    }

    printf("[mandelbrot serial]:\t\t[%.3f] ms\n", minSerial * 1000);
    writePPMImage(output_serial, width, height, "mandelbrot-serial.ppm", maxIterations);

    //
    // Run the threaded version
    //

    double minThread = 1e30;
    for (int i = 0; i < 5; ++i) {
      memset(output_thread, 0, width * height * sizeof(int));
        double startTime = CycleTimer::currentSeconds();
        mandelbrotThread(numThreads, x0, y0, x1, y1, width, height, maxIterations, output_thread);
        double endTime = CycleTimer::currentSeconds();
        minThread = std::min(minThread, endTime - startTime);
    }

    printf("[mandelbrot thread]:\t\t[%.3f] ms\n", minThread * 1000);
    writePPMImage(output_thread, width, height, "mandelbrot-thread.ppm", maxIterations);

    if (! verifyResult (output_serial, output_thread, width, height)) {
        printf ("Error : Output from threads does not match serial output\n");

        delete[] output_serial;
        delete[] output_thread;

        return 1;
    }

    // compute speedup
    printf("\t\t\t\t(%.2fx speedup from %d threads)\n", minSerial/minThread, numThreads);

    delete[] output_serial;
    delete[] output_thread;

    return 0;
}
