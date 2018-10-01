#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <irq.h>
#include <uart.h>
#include <time.h>
#include <system.h>
#include <generated/csr.h>
#include <generated/mem.h>
#include "flags.h"

#ifdef CSR_HDMI_IN1_BASE

#include "hdmi_in1.h"

#ifdef IDELAYCTRL_CLOCK_FREQUENCY
static int idelay_freq = IDELAYCTRL_CLOCK_FREQUENCY;
#else
static int idelay_freq = 200000000; // default to 200 MHz
#endif

int hdmi_in1_debug = 0;
int hdmi_in1_fb_index = 0;

#define FRAMEBUFFER_COUNT 2
#define FRAMEBUFFER_MASK (FRAMEBUFFER_COUNT - 1)

//#define HDMI_IN1_FRAMEBUFFERS_BASE (0x00000000 + 0x100000)
#define HDMI_IN1_FRAMEBUFFERS_BASE (0x06000000)
#define HDMI_IN1_FRAMEBUFFERS_SIZE (1920*1080*4)

//#define CLEAN_COMMUTATION
#define DEBUG

#define HDMI_IN1_PHASE_ADJUST_WER_THRESHOLD 10

unsigned int hdmi_in1_framebuffer_base(char n) {
	return HDMI_IN1_FRAMEBUFFERS_BASE + n*HDMI_IN1_FRAMEBUFFERS_SIZE;
}

#ifdef HDMI_IN1_INTERRUPT
static int hdmi_in1_fb_slot_indexes[2];
static int hdmi_in1_next_fb_index;
#endif

static int hdmi_in1_hres, hdmi_in1_vres;

extern void processor_update(void);

unsigned int isr_iter = 0;
extern volatile int cur_irq_mask;

#define SLOT1 1

#ifdef HDMI_IN1_INTERRUPT
void hdmi_in1_isr(void)
{
	int fb_index = -1;
	int length;
	int expected_length;
	unsigned int address_min, address_max, address;
	unsigned int stat;

	stat = hdmi_in1_dma_ev_pending_read(); // see which slot is pending

	cur_irq_mask = irq_getmask();
	//	if( isr_iter % 29 == 0 )
	//	  printf( "%d.%d.%x.%x ", stat, isr_iter, cur_irq_mask, irq_pending() );
	isr_iter++;

	// check address base/bounds
	address_min = HDMI_IN1_FRAMEBUFFERS_BASE & 0x0fffffff;
	address_max = address_min + HDMI_IN1_FRAMEBUFFERS_SIZE*FRAMEBUFFER_COUNT;
	address = hdmi_in1_dma_slot0_address_read();
	if((hdmi_in1_dma_slot0_status_read() == DVISAMPLER_SLOT_PENDING)
		&& ((address < address_min) || (address > address_max)))
	  printf("hdmi_in1: slot0: stray DMA at %08x\r\n", address);

#if SLOT1
	address = hdmi_in1_dma_slot1_address_read();
	if((hdmi_in1_dma_slot1_status_read() == DVISAMPLER_SLOT_PENDING)
		&& ((address < address_min) || (address > address_max)))
	  printf("hdmi_in1: slot1: stray DMA at %08x\r\n", address);
#endif

#ifdef CLEAN_COMMUTATION
	if((hdmi_in1_resdetection_hres_read() != hdmi_in1_hres)
	  || (hdmi_in1_resdetection_vres_read() != hdmi_in1_vres)) {
		/* Dump frames until we get the expected resolution */
		if(hdmi_in1_dma_slot0_status_read() == DVISAMPLER_SLOT_PENDING) {
			hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[0]));
			hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);
		}
#if SLOT1
		if(hdmi_in1_dma_slot1_status_read() == DVISAMPLER_SLOT_PENDING) {
			hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[1]));
			hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
		}
#endif
		return;
	}
#endif

	expected_length = hdmi_in1_hres*hdmi_in1_vres*4;
	
	if(hdmi_in1_dma_slot0_status_read() == DVISAMPLER_SLOT_PENDING) {
	  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_EMPTY);
	  length = hdmi_in1_dma_slot0_address_read() - (hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[0]) & 0x0fffffff);
	  if(length == expected_length) {
	    fb_index = hdmi_in1_fb_slot_indexes[0];
	    hdmi_in1_fb_slot_indexes[0] = hdmi_in1_next_fb_index;
	    hdmi_in1_next_fb_index = (hdmi_in1_next_fb_index + 1) & FRAMEBUFFER_MASK;
	  } else {
#ifdef DEBUG
	    printf("hdmi_in1: slot0: unexpected frame length: %d\r\n", length);
#endif
	  }
	  
	  hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[0]));
	  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);
	  hdmi_in1_dma_ev_pending_write(1);   // clear the pending slot for this channel
	  
	} else if( stat & 0x1 ) {
	  hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[0]));
	  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);
	  hdmi_in1_dma_ev_pending_write(1);  
	  printf("hdmi_in1: slot0: interrupt event but DMA wasn't pending\n");
	}

#if SLOT1	
	if(hdmi_in1_dma_slot1_status_read() == DVISAMPLER_SLOT_PENDING) {
	  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_EMPTY);
	  length = hdmi_in1_dma_slot1_address_read() - (hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[1]) & 0x0fffffff);
	  if(length == expected_length) {
	    fb_index = hdmi_in1_fb_slot_indexes[1];
	    hdmi_in1_fb_slot_indexes[1] = hdmi_in1_next_fb_index;
	    hdmi_in1_next_fb_index = (hdmi_in1_next_fb_index + 1) & FRAMEBUFFER_MASK;
	  } else {
#ifdef DEBUG
	    printf("hdmi_in1: slot1: unexpected frame length: %d\r\n", length);
#endif
	  }
	  hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[1]));
	  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
	  hdmi_in1_dma_ev_pending_write(2);   // clear the pending slot for this channel
	  
	} else if( stat & 0x2 ) {
	  hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_slot_indexes[1]));
	  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
	  hdmi_in1_dma_ev_pending_write(2);   // clear the pending slot for this channel
	  printf("hdmi_in1: slot1: interrupt event but DMA wasn't pending\n");
	}

	if(fb_index != -1) {
	  hdmi_in1_fb_index = fb_index;
	}
#endif
	
	// processor_update(); // this just does the below line
	hdmi_core_out0_initiator_base_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_index));

}
#endif

static int hdmi_in1_connected;
static int hdmi_in1_locked;

void hdmi_in1_init_video(int hres, int vres)
{
	hdmi_in1_clocking_mmcm_reset_write(1);
	hdmi_in1_connected = hdmi_in1_locked = 0;
	hdmi_in1_hres = hres; hdmi_in1_vres = vres;

#ifdef  HDMI_IN1_INTERRUPT
	unsigned int mask;

	hdmi_in1_dma_frame_size_write(hres*vres*4);
	hdmi_in1_fb_slot_indexes[0] = 0;
	hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(0));
	hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);

	hdmi_in1_fb_slot_indexes[1] = 1;
	hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(1));
	hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
	
	hdmi_in1_next_fb_index = 1;
	hdmi_in1_fb_index = 0;

	hdmi_in1_dma_ev_pending_write(hdmi_in1_dma_ev_pending_read());
	hdmi_in1_dma_ev_enable_write(0x1);
	mask = irq_getmask();
	mask |= 1 << HDMI_IN1_INTERRUPT;
	irq_setmask(mask);

#endif
}

void hdmi_in1_disable(void)
{
#ifdef HDMI_IN1_INTERRUPT
	unsigned int mask;

	mask = irq_getmask();
	mask &= ~(1 << HDMI_IN1_INTERRUPT);
	irq_setmask(mask);

	hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_EMPTY);
	hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_EMPTY);
#endif
	hdmi_in1_clocking_mmcm_reset_write(1);
}

void hdmi_in1_clear_framebuffers(void)
{
	int i;
	flush_l2_cache();

	volatile unsigned int *framebuffer = (unsigned int *)(MAIN_RAM_BASE + HDMI_IN1_FRAMEBUFFERS_BASE);
	for(i=0; i<(HDMI_IN1_FRAMEBUFFERS_SIZE*FRAMEBUFFER_COUNT)/4; i++) {
		framebuffer[i] = 0x80108010; /* black in YCbCr 4:2:2*/
	}
}

int hdmi_in1_d0, hdmi_in1_d1, hdmi_in1_d2;

void hdmi_in1_print_status(void)
{
	hdmi_in1_data0_wer_update_write(1);
	hdmi_in1_data1_wer_update_write(1);
	hdmi_in1_data2_wer_update_write(1);
	printf("hdmi_in1: ph:%4d %4d %4d // charsync:%d%d%d [%d %d %d] // WER:%3d %3d %3d // chansync:%d // res:%dx%d\r\n",
		hdmi_in1_d0, hdmi_in1_d1, hdmi_in1_d2,
		hdmi_in1_data0_charsync_char_synced_read(),
		hdmi_in1_data1_charsync_char_synced_read(),
		hdmi_in1_data2_charsync_char_synced_read(),
		hdmi_in1_data0_charsync_ctl_pos_read(),
		hdmi_in1_data1_charsync_ctl_pos_read(),
		hdmi_in1_data2_charsync_ctl_pos_read(),
		hdmi_in1_data0_wer_value_read(),
		hdmi_in1_data1_wer_value_read(),
		hdmi_in1_data2_wer_value_read(),
		hdmi_in1_chansync_channels_synced_read(),
		hdmi_in1_resdetection_hres_read(),
		hdmi_in1_resdetection_vres_read());
}

int hdmi_in1_calibrate_delays(int freq)
{
	int i, phase_detector_delay;
	int iodelay_tap_duration;

	if( idelay_freq == 400000000 ) {
	  iodelay_tap_duration = 39;
	} else {
	  iodelay_tap_duration = 78;
	}

	hdmi_in1_data0_cap_dly_ctl_write(DVISAMPLER_DELAY_RST);
	hdmi_in1_data1_cap_dly_ctl_write(DVISAMPLER_DELAY_RST);
	hdmi_in1_data2_cap_dly_ctl_write(DVISAMPLER_DELAY_RST);
	hdmi_in1_data0_cap_phase_reset_write(1);
	hdmi_in1_data1_cap_phase_reset_write(1);
	hdmi_in1_data2_cap_phase_reset_write(1);
	hdmi_in1_d0 = hdmi_in1_d1 = hdmi_in1_d2 = 0;

	/* preload slave phase detector idelay with 90° phase shift
	  (78 ps taps on 7-series) */
	// 148.5 pixclk * 10 = 1485MHz bitrate = 0.673ns window
	// 10e6/(2*freq*39) = 8 = 312 ps delay
	phase_detector_delay = 10000000/(2*freq*iodelay_tap_duration) + 3; // <<<< why is this +3 necessary?
	printf("HDMI in1 calibrate delays @ %dMHz, %d taps\n", freq, phase_detector_delay);
	for(i=0; i<phase_detector_delay; i++) {
		hdmi_in1_data0_cap_dly_ctl_write(DVISAMPLER_DELAY_SLAVE_INC);
		hdmi_in1_data1_cap_dly_ctl_write(DVISAMPLER_DELAY_SLAVE_INC);
		hdmi_in1_data2_cap_dly_ctl_write(DVISAMPLER_DELAY_SLAVE_INC);
	}
	return 1;
}

int hdmi_in1_adjust_phase(void)
{
	switch(hdmi_in1_data0_cap_phase_read()) {
		case DVISAMPLER_TOO_LATE:
			hdmi_in1_data0_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_DEC |
				                             DVISAMPLER_DELAY_SLAVE_DEC);
			hdmi_in1_d0--;
			hdmi_in1_data0_cap_phase_reset_write(1);
			break;
		case DVISAMPLER_TOO_EARLY:
			hdmi_in1_data0_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_INC |
				                             DVISAMPLER_DELAY_SLAVE_INC);
			hdmi_in1_d0++;
			hdmi_in1_data0_cap_phase_reset_write(1);
			break;
	}
	switch(hdmi_in1_data1_cap_phase_read()) {
		case DVISAMPLER_TOO_LATE:
			hdmi_in1_data1_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_DEC |
				                             DVISAMPLER_DELAY_SLAVE_DEC);
			hdmi_in1_d1--;
			hdmi_in1_data1_cap_phase_reset_write(1);
			break;
		case DVISAMPLER_TOO_EARLY:
			hdmi_in1_data1_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_INC |
				                             DVISAMPLER_DELAY_SLAVE_INC);
			hdmi_in1_d1++;
			hdmi_in1_data1_cap_phase_reset_write(1);
			break;
	}
	switch(hdmi_in1_data2_cap_phase_read()) {
		case DVISAMPLER_TOO_LATE:
			hdmi_in1_data2_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_DEC |
				                             DVISAMPLER_DELAY_SLAVE_DEC);
			hdmi_in1_d2--;
			hdmi_in1_data2_cap_phase_reset_write(1);
			break;
		case DVISAMPLER_TOO_EARLY:
			hdmi_in1_data2_cap_dly_ctl_write(DVISAMPLER_DELAY_MASTER_INC |
				                             DVISAMPLER_DELAY_SLAVE_INC);
			hdmi_in1_d2++;
			hdmi_in1_data2_cap_phase_reset_write(1);
			break;
	}
	return 1;
}

int hdmi_in1_init_phase(void)
{
	int o_d0, o_d1, o_d2;
	int i, j;

	for(i=0;i<100;i++) {
		o_d0 = hdmi_in1_d0;
		o_d1 = hdmi_in1_d1;
		o_d2 = hdmi_in1_d2;
		for(j=0;j<1000;j++) {
			if(!hdmi_in1_adjust_phase())
				return 0;
		}
		if((abs(hdmi_in1_d0 - o_d0) < 4) && (abs(hdmi_in1_d1 - o_d1) < 4) && (abs(hdmi_in1_d2 - o_d2) < 4))
			return 1;
	}
	return 0;
}

int hdmi_in1_phase_startup(int freq)
{
	int ret;
	int attempts;

	attempts = 0;
	while(1) {
		attempts++;
		hdmi_in1_calibrate_delays(freq);
		if(hdmi_in1_debug)
			printf("hdmi_in1: delays calibrated\r\n");
		ret = hdmi_in1_init_phase();
		if(ret) {
			if(hdmi_in1_debug)
				printf("hdmi_in1: phase init OK\r\n");
			return 1;
		} else {
			printf("hdmi_in1: phase init failed\r\n");
			if(attempts > 3) {
				printf("hdmi_in1: giving up\r\n");
				hdmi_in1_calibrate_delays(freq);
				return 0;
			}
		}
	}
}

static void hdmi_in1_check_overflow(void)
{
#ifdef HDMI_IN1_INTERRUPT
	if(hdmi_in1_frame_overflow_read()) {
		printf("hdmi_in1: FIFO overflow\r\n");
		hdmi_in1_frame_overflow_write(1);
	}
#endif
}

static int hdmi_in1_clocking_locked_filtered(void)
{
	static int lock_start_time;
	static int lock_status;

	if(hdmi_in1_clocking_locked_read()) {
		switch(lock_status) {
			case 0:
				elapsed(&lock_start_time, -1);
				lock_status = 1;
				break;
			case 1:
				if(elapsed(&lock_start_time, SYSTEM_CLOCK_FREQUENCY/4))
					lock_status = 2;
				break;
			case 2:
				return 1;
		}
	} else
		lock_status = 0;
	return 0;
}

static int hdmi_in1_get_wer(void){
	int wer = 0;
	wer += hdmi_in1_data0_wer_value_read();
	wer += hdmi_in1_data1_wer_value_read();
	wer += hdmi_in1_data2_wer_value_read();
	return wer;
} 

#if 0
unsigned int service_count = 0;
void service_dma(void) {
#if 0
  flush_cpu_icache();
  flush_cpu_dcache();
  if(hdmi_in1_dma_slot0_status_read() == DVISAMPLER_SLOT_PENDING) {
	  hdmi_in1_dma_frame_size_write(1920*1080*4);
	  hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(0));
	  hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(1));
	  printf( "slot0 %x s%d ", hdmi_in1_dma_slot0_address_read(), service_count++ );
	  //	  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);
	  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_EMPTY);
	}
  flush_cpu_icache();
  flush_cpu_dcache();
#else

  // self-start
  if(hdmi_in1_dma_address_valid_read() == 0) {
    hdmi_in1_dma_address_valid_write(1);
  }

  // restart on vsync
  if(hdmi_in1_dma_dma_running_read() == 0 && hdmi_in1_dma_address_valid_read() == 1) {
    hdmi_in1_dma_dma_go_write(1);
    if( hdmi_in1_dma_last_count_reached_read() != (hdmi_in1_framebuffer_base(0) + 1920*1080*4) / 32 )
      printf( "DMA count err: last %x, %d ", hdmi_in1_dma_last_count_reached_read(), service_count++ );
  }
#endif
}
#endif

void hdmi_in1_service(int freq)
{
	static int last_event;

	if(hdmi_in1_connected) {
	  if(!hdmi_in1_edid_hpd_notif_read()) {
	    if(hdmi_in1_debug)
	      printf("hdmi_in1: disconnected\r\n");
	    hdmi_in1_connected = 0;
	    hdmi_in1_locked = 0;
	    hdmi_in1_clocking_mmcm_reset_write(1);
	    //			hdmi_in1_clear_framebuffers();
	  } else {
	    if(hdmi_in1_locked) {
	      if(hdmi_in1_clocking_locked_filtered()) {
		//		service_dma();
		if(elapsed(&last_event, SYSTEM_CLOCK_FREQUENCY/4)) {
		  hdmi_in1_data0_wer_update_write(1);
		  hdmi_in1_data1_wer_update_write(1);
		  hdmi_in1_data2_wer_update_write(1);
		  if(hdmi_in1_debug)
		    hdmi_in1_print_status();
		  if(hdmi_in1_get_wer() >= HDMI_IN1_PHASE_ADJUST_WER_THRESHOLD)
		    hdmi_in1_adjust_phase();
		}
	      } else {
		if(hdmi_in1_debug)
		  printf("hdmi_in1: lost PLL lock\r\n");
		hdmi_in1_locked = 0;
		//					hdmi_in1_clear_framebuffers();
	      }
	    } else {
	      if(hdmi_in1_clocking_locked_filtered()) {
		if(hdmi_in1_debug)
		  printf("hdmi_in1: PLL locked\r\n");
		hdmi_in1_phase_startup(freq);
		if(hdmi_in1_debug)
		  hdmi_in1_print_status();
		hdmi_in1_locked = 1;
	      }
	    }
	  }
	} else {
	  if(hdmi_in1_edid_hpd_notif_read()) {
	    if(hdmi_in1_debug)
	      printf("hdmi_in1: connected\r\n");
	    hdmi_in1_connected = 1;
	    hdmi_in1_clocking_mmcm_reset_write(0);
	  }
	}
	hdmi_in1_check_overflow();
}

#endif
