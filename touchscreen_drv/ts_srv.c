/*
* This is a userspace touchscreen driver for cypress ctma395 as used
* in HP Touchpad configured for WebOS.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 and
* only version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301, USA.
*
* The code was written from scrath, the hard math and understanding the
* device output by jonpry @ gmail
* uinput bits and the rest by Oleg Drokin green@linuxhacker.ru
* Multitouch detection by Rafael Brune mail@rbrune.de
*
* Copyright (c) 2011 CyanogenMod Touchpad Project.
*
*
*/

#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/hsuart.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/select.h>

#if 1
// This is for Android
#define UINPUT_LOCATION "/dev/uinput"
#else
// This is for webos and possibly other Linuxes
#define UINPUT_LOCATION "/dev/input/uinput"
#endif

/* Set to 1 to print coordinates to stdout. 

#define DEBUG 0
#define RAW_DATA_DEBUG 0
// removes values below threshold for easy reading, set to 0 to see everything
// a value of 2 should remove most unwanted output
#define RAW_DATA_THRESHOLD 0

#define AVG_FILTER 1

#define USERSPACE_270_ROTATE 0

#define RECV_BUF_SIZE 1540
#define LIFTOFF_TIMEOUT 25000

#define MAX_TOUCH 10
#define MAX_CLIST 75
#define MAX_DELTA 25 // this value is squared to prevent the need to use sqrt
#define TOUCH_THRESHOLD 28 // threshold for what is considered a valid touch

// enables filtering of larger touch areas like the side of your thumb into
// a single touch
#define LARGE_AREA_FILTER 1
#define LARGE_AREA_THRESHOLD 52 // threshold to invoke the large area filter
#define LARGE_AREA_UNPRESS 32 // threshold for end of the large area

// enables filtering of a single touch to make it easier to long press
// keeps the initial touch point the same so long as it stays within
// the radius
#define DEBOUNCE_FILTER 1
#define DEBOUNCE_RADIUS 2 // radius for debounce in pixels

*/
#define MAX_TOUCH 10
unsigned char cline[64];
unsigned int cidx=0;
unsigned char matrix[30][40];
int invalid_matrix[30][40];
int uinput_fd;

/* mschnee: wrapper for ts configuration loading 
* This is a quick-and-dirty proof-of-concept implementation
* 
*/

/* change this to whatever the actual file is going to be */
#define CONFIG_FILE "/system/ts_srv.cfg" 
#define LINE_MAX 32
typedef enum configToken { 
    DEBUG, RAW_DATA_DEBUG, RAW_DATA_THRESHOLD, AVG_FILTER, USERSPACE_270_ROTATE, RECV_BUF_SIZE,
    LIFTOFF_TIMEOUT, MAX_CLIST, MAX_DELTA, TOUCH_THRESHOLD,
    LARGE_AREA_FILTER, LARGE_AREA_THRESHOLD, LARGE_AREA_UNPRESS,
    DEBOUNCE_FILTER, DEBOUNCE_RADIUS
} _cfg;

#define sc(a,b) strstr(a,b)!=NULL
/**
*	A small wrapper to load settings from a file or use defaults, isntead
*	if #define'ing them.
*	@param configToken t enum See the enum declared above.
*	@return int com
*/
int get_config(_cfg t) {
    static int 
        s_opened = 0,
        s_debug = 0,
        s_raw_data_debug = 0,
		s_raw_data_threshold = 0,
        s_avg_filter = 1,
        s_userspace_270_filter = 0,
        s_recv_buf_size = 1540,
        s_liftoff_timeout = 25000,
        s_max_touch = 10,
        s_max_clist =75,
        s_max_delta = 25,
        s_touch_threshold = 24,
	s_large_area_filter = 1, 
	s_large_area_threshold = 52, 
	s_large_area_unpress = 32,
	s_deboune_filter = 1, 
	s_debounce_radius = 2;

    /* this block should only run once at startup */
    if(!s_opened) {
        s_opened = 1;
	int settings_fp = open(CONFIG_FILE,O_RDONLY);
        if( settings_fp ){
            /* allocate buffer */
            char buff[LINE_MAX] = {0};

            /* read */
            while(fgets(buff,LINE_MAX,settings_fp) != NULL) {
                if(sc(buff,"DEBUG="))
                    s_debug = atoi(buff+6);
                else if(sc(buff,"RAW_DATA_DEBUG="))
                    s_raw_data_debug = atoi(buff+15);
				else if(sc(buff,"RAW_DATA_THRESHOLD="))
                    s_raw_data_threshold = atoi(buff+19);
                else if(sc(buff,"AVG_FILTER="))
                    s_avg_filter = atoi(buff+11);
                else if(sc(buff,"USERSPACE_270_FILTER="))
                    s_userspace_270_filter = atoi(buff+21);
                else if(sc(buff,"RECV_BUF_SIZE="))
                    s_recv_buf_size = atoi(buff+14);
                else if(sc(buff,"LIFTOFF_TIMEOUT="))
                    s_liftoff_timeout = atoi(buff+16);
                else if(sc(buff,"MAX_TOUCH="))
                    s_max_touch = atoi(buff+10);
                else if(sc(buff,"MAX_CLIST="))
                    s_max_clist = atoi(buff+10);
                else if(sc(buff,"MAX_DELTA="))
                    s_max_delta = atoi(buff+10);
                else if(sc(buff,"TOUCH_THRESHOLD="))
                    s_touch_threshold = atoi(buff+16);
		else if(sc(buff,"LARGE_AREA_FILTER="))
                    s_touch_threshold = atoi(buff+18);
		else if(sc(buff,"LARGE_AREA_THRESHOLD="))
                    s_touch_threshold = atoi(buff+21);
		else if(sc(buff,"LARGE_AREA_UNPRESS="))
                    s_touch_threshold = atoi(buff+19);
		else if(sc(buff,"DEBOUNCE_FILTER="))
                    s_touch_threshold = atoi(buff+16);
		else if(sc(buff,"DEBOUNCE_RADIUS="))
                    s_touch_threshold = atoi(buff+16);
            }
        } else {
            /*
	     * some kind of error opening the file, such as
	     * the file not existing 
	     */
            
        }

    }
    switch(t) {
      case DEBUG: return s_debug; break;
      case RAW_DATA_DEBUG: return s_raw_data_debug; break;
	  case RAW_DATA_THRESHOLD: return s_raw_data_threshold; break;
      case AVG_FILTER: return s_avg_filter; break;
      case USERSPACE_270_ROTATE: return s_userspace_270_filter; break;
      case RECV_BUF_SIZE: return s_recv_buf_size; break;
      case LIFTOFF_TIMEOUT: return s_liftoff_timeout; break;
      case MAX_CLIST: return s_max_clist; break;
      case MAX_DELTA: return s_max_delta; break;
      case TOUCH_THRESHOLD: return s_touch_threshold; break;
      case LARGE_AREA_FILTER: return s_large_area_filter; break;
      case LARGE_AREA_THRESHOLD: return s_large_area_threshold; break;
      case LARGE_AREA_UNPRESS: return s_large_area_unpress; break;
      case DEBOUNCE_FILTER: return s_deboune_filter; break;
      case DEBOUNCE_RADIUS: return s_debounce_radius; break;
      default: return 0;
    }
    
    // unnecessary fallback, but can silence some compiler warnings.
    return 0;

}
#undef sc

int send_uevent(int fd, __u16 type, __u16 code, __s32 value)
{
    struct input_event event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;

    if (write(fd, &event, sizeof(event)) != sizeof(event)) {
        fprintf(stderr, "Error on send_event %d", sizeof(event));
        return -1;
    }

    return 0;
}

struct candidate {
    int pw;
    int i;
    int j;
};

struct touchpoint {
    int pw;
    float i;
    float j;
    unsigned short isValid;
};

int tpcmp(const void *v1, const void *v2)
{
    return ((*(struct candidate *)v2).pw - (*(struct candidate *)v1).pw);
}
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))
#define isBetween(A, B, C) ( ((A-B) > 0) && ((A-C) < 0) )

int dist(int x1, int y1, int x2, int y2)  {
    return pow(x1 - x2, 2)+pow(y1 - y2, 2);
}

struct touchpoint tpoint[MAX_TOUCH];
struct touchpoint prevtpoint[MAX_TOUCH];
struct touchpoint prev2tpoint[MAX_TOUCH];


int isClose(struct touchpoint a, struct touchpoint b)
{
    if (isBetween(b.i, a.i+2.5, a.i-2.5) && isBetween(b.j, a.j+2.5, a.j-2.5))
        return 1;
    return 0;
}

//return 1 if b is closer
//return 2 if c is closer
int find_closest(struct touchpoint a, struct touchpoint b, struct touchpoint c)
{
    int diffB = fabs(a.i - b.i) + fabs(a.j - b.j);
    int diffC = fabs(a.i - c.i) + fabs(a.j - c.j);

    if (diffB < diffC)
        return 1;
    else
        return 2;
}

int avg_filter(struct touchpoint *t) {
    int tp1_found, tp2_found, i;
    tp1_found = tp2_found = -1;

    for(i=0; i<get_config(MAX_TOUCH); i++) {
        if(isClose(*t, prevtpoint[i])) {
            if(tp1_found < 0) {
                tp1_found = i;
            } else {
                if (find_closest(*t, prevtpoint[tp1_found], prevtpoint[i]) == 2)
                    tp1_found = i;
            }
        }
        if(isClose(*t, prev2tpoint[i])) {
            if(tp2_found < 0) {
                tp2_found = i;
            } else {
                if (find_closest(*t, prev2tpoint[tp2_found], prev2tpoint[i]) == 2)
                    tp2_found = i;
            }
        }
    }
if(get_config(DEBUG))
    printf("before: i=%f, j=%f", t->i, t->j);
    if (tp1_found >= 0 && tp2_found >= 0) {
        t->i = (t->i + prevtpoint[tp1_found].i + prev2tpoint[tp2_found].i) / 3.0;
        t->j = (t->j + prevtpoint[tp1_found].j + prev2tpoint[tp2_found].j) / 3.0;
    }
if(get_config(DEBUG))
    printf("|||| after: i=%f, j=%f\n", t->i, t->j);

    return 0;
}


void liftoff(void)
{
    // sends liftoff events - nothing is touching the screen
    send_uevent(uinput_fd, EV_KEY, BTN_TOUCH, 0);
    send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
    send_uevent(uinput_fd, EV_SYN, SYN_REPORT, 0);
}


void check_large_area_points(int *mini, int *maxi, int *minj, int *maxj, int i, int j){
	// invalidate this touch point so that we don't process it later
	invalid_matrix[i][j] = 1;
	// update min and max values for i and j if needed
	if (i < *mini)
		*mini = i;
	if (i > *maxi)
		*maxi = i;
	if (j < *minj)
		*minj = j;
	if (j > *maxj)
		*maxj = j;
	// check nearby points to see if they are above LARGE_AREA_UNPRESS
	if(i>0  && !invalid_matrix[i-1][j] && matrix[i-1][j] > get_config(LARGE_AREA_UNPRESS))
		check_large_area_points(mini, maxi, minj, maxj, i-1, j);
	if(i<29 && !invalid_matrix[i+1][j] && matrix[i+1][j] > get_config(LARGE_AREA_UNPRESS))
		check_large_area_points(mini, maxi, minj, maxj, i+1, j);
	if(j>0  && !invalid_matrix[i][j-1] && matrix[i][j-1] > get_config(LARGE_AREA_UNPRESS))
		check_large_area_points(mini, maxi, minj, maxj, i, j-1);
	if(j<39 && !invalid_matrix[i][j+1] && matrix[i][j+1] > get_config(LARGE_AREA_UNPRESS))
		check_large_area_points(mini, maxi, minj, maxj, i, j+1);
}


int calc_point(void)
{
	int i,j;
	int tweight=0;
	int tpc=0;
	float isum=0, jsum=0;
	float avgi, avgj;
	float powered;
	static int previoustpc;
	int clc=0;
	struct candidate clist[MAX_CLIST];
	int new_debounce_touch=0;
	static float initiali, initialj;

	if(get_config(DEBOUNCE_FILTER)) {
		if (tpoint[0].i < 0)
			new_debounce_touch=1;
	}
	
    //Record values for processing later
	for(i=0; i < previoustpc; i++) {
		prev2tpoint[i].i = prevtpoint[i].i;
		prev2tpoint[i].j = prevtpoint[i].j;
		prev2tpoint[i].pw = prevtpoint[i].pw;
		prevtpoint[i].i = tpoint[i].i;
		prevtpoint[i].j = tpoint[i].j;
		prevtpoint[i].pw = tpoint[i].pw;
	}

	// generate list of high values
	if(get_config(LARGE_AREA_FILTER)) 
	  memset(&invalid_matrix, 0, sizeof(invalid_matrix));

	for(i=0; i < 30; i++) {
		for(j=0; j < 40; j++) {
		if(get_config(RAW_DATA_DEBUG)){
			if (matrix[i][j] < get_config(RAW_DATA_THRESHOLD))
				printf("   ");
			else
				printf("%2.2X ", matrix[i][j]);
		}

			if (clc < MAX_CLIST) {
				if(get_config(LARGE_AREA_FILTER)) {
					if(matrix[i][j] > get_config(LARGE_AREA_THRESHOLD) && !invalid_matrix[i][j]) {
						// this is a large area press (e.g. the side of your thumb)
						// so we will scan all the points nearby and if they are
						// above the LARGE_AREA_UNPRESS we mark them invalid and
						// track the min and max i,j location so that we can
						// calculate a center for the large area press
						int mini=i,maxi=i,minj=j,maxj=j;
						check_large_area_points(&mini, &maxi, &minj, &maxj, i, j);
						int centeri, centerj;
						// calculate the center
						centeri = mini + ((maxi - mini) / 2);
						centerj = minj + ((maxj - minj) / 2);
						// set the point to the center
						clist[clc].i = centeri;
						clist[clc].j = centerj;
						clist[clc].pw = matrix[centeri][centerj];
						clc++;
					}else if (!invalid_matrix[i][j])	{
						if(matrix[i][j] > get_config(TOUCH_THRESHOLD)) {
							int cvalid=1;
							// check if local maxima
							if(i>0  && matrix[i-1][j] > matrix[i][j]) cvalid = 0;
							if(i<29 && matrix[i+1][j] > matrix[i][j]) cvalid = 0;
							if(j>0  && matrix[i][j-1] > matrix[i][j]) cvalid = 0;
							if(j<39 && matrix[i][j+1] > matrix[i][j]) cvalid = 0;
							if(cvalid) {
								clist[clc].pw = matrix[i][j];
								clist[clc].i = i;
								clist[clc].j = j;
								clc++;
							}
						}
					}
				} else {
					if(matrix[i][j] > get_config(TOUCH_THRESHOLD)) {
						int cvalid=1;
						// check if local maxima
						if(i>0  && matrix[i-1][j] > matrix[i][j]) cvalid = 0;
						if(i<29 && matrix[i+1][j] > matrix[i][j]) cvalid = 0;
						if(j>0  && matrix[i][j-1] > matrix[i][j]) cvalid = 0;
						if(j<39 && matrix[i][j+1] > matrix[i][j]) cvalid = 0;
						if(cvalid) {
							clist[clc].pw = matrix[i][j];
							clist[clc].i = i;
							clist[clc].j = j;
							clc++;
						}
					}
				}
				
			}
		}
		if(get_config(RAW_DATA_DEBUG))
			printf("|\n");

	}
	if(get_config(RAW_DATA_DEBUG))
		printf("end of raw data\n"); // helps separate one frame from the next


	if(get_config(DEBUG))
		printf("%d %d %d \n", clist[0].pw, clist[1].pw, clist[2].pw);

    // sort candidate list by strength
    //qsort(clist, clc, sizeof(clist[0]), tpcmp);

	if(get_config(DEBUG))
		printf("%d %d %d \n", clist[0].pw, clist[1].pw, clist[2].pw);

    int k, l;
    for(k=0; k < MIN(clc, 20); k++) {
        int newtp=1;

        int rad=3; // radius around candidate to use for calculation
        int mini = clist[k].i - rad+1;
        int maxi = clist[k].i + rad;
        int minj = clist[k].j - rad+1;
        int maxj = clist[k].j + rad;
        
        // discard points close to already detected touches
        for(l=0; l<tpc; l++) {
            if(tpoint[l].i >= mini+1 && tpoint[l].i < maxi-1 && tpoint[l].j >= minj+1 && tpoint[l].j < maxj-1)
                newtp=0;
		}
		
		// calculate new touch near the found candidate
		if(newtp && tpc < MAX_TOUCH) {
			tweight=0;
			isum=0;
			jsum=0;
			for(i=MAX(0, mini); i < MIN(30, maxi); i++) {
				for(j=MAX(0, minj); j < MIN(40, maxj); j++) {
					int dd = dist(i,j,clist[k].i,clist[k].j);
					powered = matrix[i][j];
					if(dd == 2 && 0.65f * matrix[clist[k].i][clist[k].j] < matrix[i][j] ) 
						powered = 0.65f * matrix[clist[k].i][clist[k].j];
					if(dd == 4 && 0.15f * matrix[clist[k].i][clist[k].j] < matrix[i][j] ) 
						powered = 0.15f * matrix[clist[k].i][clist[k].j];
					if(dd == 5 && 0.10f * matrix[clist[k].i][clist[k].j] < matrix[i][j] ) 
						powered = 0.10f * matrix[clist[k].i][clist[k].j];
					if(dd == 8 && 0.05f * matrix[clist[k].i][clist[k].j] < matrix[i][j] ) 
						powered = 0.05f * matrix[clist[k].i][clist[k].j];
					
					powered = pow(powered, 1.5);
					tweight += powered;
					isum += powered * i;
					jsum += powered * j;
				}
			}
			avgi = isum / (float)tweight;
			avgj = jsum / (float)tweight;
			tpoint[tpc].pw = tweight;
			tpoint[tpc].i = avgi;
			tpoint[tpc].j = avgj;
			tpoint[tpc].isValid = 1;
			tpc++;

			if(get_config(DEBUG))
				printf("Coords %d %lf, %lf, %d\n", tpc, avgi, avgj, tweight);
		}
	}

	/* filter touches for impossibly large moves that indicate a liftoff and
	 * re-touch */
	if (previoustpc == 1 && tpc == 1) {
		float deltai, deltaj, total_delta;
		deltai = tpoint[0].i - prevtpoint[0].i;
		deltaj = tpoint[0].j - prevtpoint[0].j;
		// calculate squared hypotenuse
		total_delta = (deltai * deltai) + (deltaj * deltaj);
		if (total_delta > MAX_DELTA)
			liftoff();
	}

	//report touches
	for (k = 0; k < tpc; k++) {
		if (tpoint[k].isValid) {
			if(get_config(AVG_FILTER))
				avg_filter(&tpoint[k]);

			if(get_config(DEBOUNCE_FILTER)) {
				// The debounce filter only works on a single touch
				// We record the initial touchdown point, calculate a radius in
				// pixels and re-center the point if we're still within the
				// radius.  Once we leave the radius, we invalidate so that we
				// don't debounce again even if we come back to the radius
				if (tpc == 1) {
					if (new_debounce_touch && k == 0) {
						// we record the initial location of a new touch
						initiali = tpoint[k].i;
						initialj = tpoint[k].j;
					} else if (initiali > -20) {
						// calculate pixel locations around the initial touch
						// and locations for the current touch
						
						int mini = (initiali*768/29)  - get_config(DEBOUNCE_RADIUS),
							maxi = (initiali*768/29)  + get_config(DEBOUNCE_RADIUS),
							minj = (initialj*1024/39) - get_config(DEBOUNCE_RADIUS),
							maxj = (initialj*1024/39) + get_config(DEBOUNCE_RADIUS),
							actuali = (tpoint[k].i*768/29),
							actualj = (tpoint[k].j*1024/39);
						// see if the current touch is still inside the debounce
						// radius
						if (actuali >= mini && actuali <= maxi && actualj >= minj && actualj <= maxj) {
							// set the point to the original point - debounce!
							tpoint[k].i = initiali;
							tpoint[k].j = initialj;
						} else {
							initiali = -100; // invalidate
						}
					}
				}
			}
			send_uevent(uinput_fd, EV_KEY, BTN_TOUCH, 1);
			if(get_config(USERSPACE_270_ROTATE)) {
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_X, tpoint[k].i*768/29);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, 1024-tpoint[k].j*1024/39);
			} else {
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, 768-tpoint[k].i*768/29);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_X, 1024-tpoint[k].j*1024/39);
			}
			send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
			tpoint[k].isValid = 0;

        }
    }
    send_uevent(uinput_fd, EV_SYN, SYN_REPORT, 0);
    previoustpc = tpc; // store the touch count for the next run
    return tpc; // return the touch count
}

int cline_valid(unsigned int extras);
void put_byte(unsigned char byte)
{
//	printf("Putc %d %d\n", cidx, byte);
    if(cidx==0 && byte != 0xFF)
        return;

    //Sometimes a send is aborted by the touch screen. all we get is an out of place 0xFF
    if(byte == 0xFF && !cline_valid(1))
        cidx = 0;
    cline[cidx++] = byte;
}

int cline_valid(unsigned int extras)
{
    if(cline[0] == 0xff && cline[1] == 0x43 && cidx == 44-extras)
    {
//		printf("cidx %d\n", cline[cidx-1]);
        return 1;
    }
    if(cline[0] == 0xff && cline[1] == 0x47 && cidx > 4 && cidx == (cline[2]+4-extras))
    {
//		printf("cidx %d\n", cline[cidx-1]);
        return 1;
    }
    return 0;
}

int consume_line(void)
{
    int i,j,ret=0;

    if(cline[1] == 0x47)
    {
        //calculate the data points. all transfers complete
        ret = calc_point();
    }

    if(cline[1] == 0x43)
    {
        //This is a start event. clear the matrix
        if(cline[2] & 0x80)
        {
            for(i=0; i < 30; i++)
                for(j=0; j < 40; j++)
                    matrix[i][j] = 0;
        }

        //Write the line into the matrix
        for(i=0; i < 40; i++)
            matrix[cline[2] & 0x1F][i] = cline[i+3];
    }

    cidx = 0;

    return ret;
}

int snarf2(unsigned char* bytes, int size)
{
    int i=0,ret=0;

    while(i < size)
    {
        put_byte(bytes[i]);
        i++;
        if(cline_valid(0))
            ret += consume_line();
    }

    return ret;
}

void open_uinput(void)
{
    struct uinput_user_dev device;

    memset(&device, 0, sizeof device);

    uinput_fd=open(UINPUT_LOCATION,O_WRONLY);
    strcpy(device.name,"HPTouchpad");

    device.id.bustype=BUS_VIRTUAL;
    device.id.vendor=1;
    device.id.product=1;
    device.id.version=1;

    if(get_config(USERSPACE_270_ROTATE)) {
	device.absmax[ABS_MT_POSITION_X]=768;
	device.absmax[ABS_MT_POSITION_Y]=1024;
    } else {
	device.absmax[ABS_MT_POSITION_X]=1024;
	device.absmax[ABS_MT_POSITION_Y]=768;
    }
    device.absmin[ABS_MT_POSITION_X]=0;
    device.absmin[ABS_MT_POSITION_Y]=0;
    device.absfuzz[ABS_MT_POSITION_X]=2;
    device.absflat[ABS_MT_POSITION_X]=0;
    device.absfuzz[ABS_MT_POSITION_Y]=1;
    device.absflat[ABS_MT_POSITION_Y]=0;

    if (write(uinput_fd,&device,sizeof(device)) != sizeof(device))
    {
        fprintf(stderr, "error setup\n");
    }

    if (ioctl(uinput_fd,UI_SET_EVBIT,EV_KEY) < 0)
        fprintf(stderr, "error evbit key\n");

    if (ioctl(uinput_fd,UI_SET_EVBIT, EV_SYN) < 0)
        fprintf(stderr, "error evbit key\n");

    if (ioctl(uinput_fd,UI_SET_EVBIT,EV_ABS) < 0)
            fprintf(stderr, "error evbit rel\n");

//    if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_TRACKING_ID) < 0)
//            fprintf(stderr, "error trkid rel\n");

/*    if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_TOUCH_MAJOR) < 0)
            fprintf(stderr, "error tool rel\n");

    if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_WIDTH_MAJOR) < 0)
            fprintf(stderr, "error tool rel\n");
*/
    if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_POSITION_X) < 0)
            fprintf(stderr, "error tool rel\n");

    if (ioctl(uinput_fd,UI_SET_ABSBIT,ABS_MT_POSITION_Y) < 0)
            fprintf(stderr, "error tool rel\n");

    if (ioctl(uinput_fd,UI_SET_KEYBIT,BTN_TOUCH) < 0)
            fprintf(stderr, "error evbit rel\n");

    if (ioctl(uinput_fd,UI_DEV_CREATE) < 0)
    {
        fprintf(stderr, "error create\n");
    }

}

void clear_arrays(void)
{
    // clears arrays (for after a total liftoff occurs
    int i;
    for(i=0; i<get_config(MAX_TOUCH); i++) {
        tpoint[i].i = -10;
        tpoint[i].j = -10;
        prevtpoint[i].i = -10;
        prevtpoint[i].j = -10;
        prev2tpoint[i].i = -10;
        prev2tpoint[i].j = -10;
    }
}

int main(int argc, char** argv)
{
    struct hsuart_mode uart_mode;
    int uart_fd, nbytes;
    unsigned char recv_buf[get_config(RECV_BUF_SIZE)];
    fd_set fdset;
    struct timeval seltmout;
    struct sched_param sparam = { .sched_priority = 99 /* linux maximum, nonportable */};

    /* We set ts server priority to RT so that there is no delay in
    * in obtaining input and we are NEVER bumped from CPU until we
    * give it up ourselves. */
    if (sched_setscheduler(0 /* that's us */, SCHED_FIFO, &sparam))
        perror("Cannot set RT priority, ignoring: ");
    
    uart_fd = open("/dev/ctp_uart", O_RDONLY|O_NONBLOCK);
    if(uart_fd<=0)
    {
        printf("Could not open uart\n");
        return 0;
    }

    open_uinput();

    ioctl(uart_fd,HSUART_IOCTL_GET_UARTMODE,&uart_mode);
    uart_mode.speed = 0x3D0900;
    ioctl(uart_fd, HSUART_IOCTL_SET_UARTMODE,&uart_mode);

    ioctl(uart_fd, HSUART_IOCTL_FLUSH, 0x9);

    while(1)
    {
        FD_ZERO(&fdset);
        FD_SET(uart_fd, &fdset);
        seltmout.tv_sec = 0;
        /* 2x tmout */
        seltmout.tv_usec = get_config(LIFTOFF_TIMEOUT);

        if (0 == select(uart_fd+1, &fdset, NULL, NULL, &seltmout)) {
            /* Timeout means liftoff, send event */
            if(get_config(DEBUG))
                printf("timeout! sending liftoff\n");

            clear_arrays();
            liftoff();

            FD_ZERO(&fdset);
            FD_SET(uart_fd, &fdset);
            /* Now enter indefinite sleep iuntil input appears */
            select(uart_fd+1, &fdset, NULL, NULL, NULL);
            /* In case we were wrongly woken up check the event
            * count again */
            continue;
        }
            
        nbytes = read(uart_fd, recv_buf, get_config(RECV_BUF_SIZE));
        
        if(nbytes <= 0)
            continue;
        if(get_config(DEBUG)) {
            printf("Received %d bytes\n", nbytes);
            int i;
            for(i=0; i < nbytes; i++)
                printf("%2.2X ",recv_buf[i]);
            printf("\n");
        }
        if (!snarf2(recv_buf,nbytes)) {
            // sometimes there is data, but no valid touches due to threshold
            clear_arrays();
            liftoff();
        }
    }

    return 0;
}
