#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "stdio_wrap.h"

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/sdram_phy.h>
#include <time.h>
#include <console.h>
#include "flags.h"

#include <irq.h>

#include "asm.h"
#include "config.h"
#include "hdmi_in0.h"
#include "hdmi_in1.h"
#include "processor.h"
#include "mmcm.h"
#include "ci.h"
#include "encoder.h"
#include "hdmi_out0.h"
#include "bist.h"
#include "dump.h"
#include "edid.h"
#include "km.h"

extern unsigned int update_video;

static int hdmi_in1_fb_slot_indexes[2];
static int hdmi_in1_next_fb_index;

int status_enabled = 0;
int json_enabled = 1;

extern const struct video_timing video_modes[];

static unsigned int log2(unsigned int v)
{
	unsigned int r = 0;
	while(v>>=1) r++;
	return r;
}

static void help_video_matrix(void)
{
	wputs("video_matrix list              - list available video sinks and sources");
	wputs("video_matrix connect <source>  - connect video source to video sink");
	wputs("                     <sink>");
}

static void help_video_mode(void)
{
	wputs("video_mode list                - list available video modes");
	wputs("video_mode <mode>              - select video mode");
}

static void help_hdp_toggle(void)
{
	wputs("hdp_toggle <source>             - toggle HDP on source for EDID rescan");
}

static void help_status(void)
{
	wputs("status                         - print status message once");
	wputs("status <on/off>                - repeatedly print status message");
}

#ifdef CSR_HDMI_OUT0_BASE
static void help_output0(void)
{
	wputs("output0 on                     - enable output0");
	wputs("output0 off                    - disable output0");
}
#endif

#ifdef CSR_HDMI_OUT1_BASE
static void help_output1(void)
{
	wputs("output1 on                     - enable output1");
	wputs("output1 off                    - disable output1");
}
#endif

#ifdef ENCODER_BASE
static void help_encoder(void)
{
	wputs("encoder on                     - enable encoder");
	wputs("encoder off                    - disable encoder");
	wputs("encoder quality <quality>      - select quality");
	wputs("encoder fps <fps>              - configure target fps");
}
#endif

#ifdef CSR_DMA_WRITER_BASE
static void help_dma_writer(void)
{
	wputs("dma_writer on                  - enable dma_writer");
	wputs("dma_writer off                 - disable dma_writer");
}
#endif

#ifdef CSR_DMA_READER_BASE
static void help_dma_reader(void)
{
	wputs("dma_reader on                  - enable dma_reader");
	wputs("dma reader off                 - disable dma_reader");
}
#endif

#ifdef CSR_GENERATOR_BASE
static void help_sdram_test(void)
{
	wputs("sdram_test                     - Run SDRAM BIST checker");

}
#endif

static void help_debug(void)
{
	wputs("debug mmcm                     - dump mmcm configuration");
#ifdef CSR_SDRAM_CONTROLLER_BANDWIDTH_UPDATE_ADDR
	wputs("debug ddr                      - show DDR bandwidth");
#endif
	wputs("debug dna                      - show Board's DNA");
	wputs("debug edid                     - dump monitor EDID");
}

static void ci_help(void)
{
	wputs("help                           - this command");
	wputs("reboot                         - reboot CPU");
	wputs("");
	wputs("mr                             - read address space");
	wputs("mw                             - write address space");
	wputs("mc                             - copy address space");
	wputs("");
	help_status();
	wputs("");
	help_video_matrix();
	wputs("");
	help_video_mode();
	wputs("");
	help_hdp_toggle();
	wputs("");
#ifdef CSR_HDMI_OUT0_BASE
	help_output0();
	wputs("");
#endif
#ifdef CSR_HDMI_OUT1_BASE
	help_output1();
	wputs("");
#endif
#ifdef ENCODER_BASE
	help_encoder();
	wputs("");
#endif
#ifdef CSR_DMA_WRITER_BASE
	help_dma_writer();
	wputs("");
#endif
#ifdef CSR_DMA_READER_BASE
	help_dma_reader();
	wputs("");
#endif
#ifdef CSR_GENERATOR_BASE
	help_sdram_test();
	wputs("");
#endif
	wputs("");
	help_debug();
}

static char *readstr(void)
{
	char c[2];
	static char s[64];
	static int ptr = 0;

	if(readchar_nonblock()) {
		c[0] = readchar();
		c[1] = 0;
		switch(c[0]) {
			case 0x7f:
			case 0x08:
				if(ptr > 0) {
					ptr--;
					wputsnonl("\x08 \x08");
				}
				break;
			case 0x07:
				break;
			case '\r':
			case '\n':
				s[ptr] = 0x00;
				wputsnonl("\n");
				ptr = 0;
				return s;
			default:
				if(ptr >= (sizeof(s) - 1))
					break;
				wputsnonl(c);
				s[ptr] = c[0];
				ptr++;
				break;
		}
	}

	return NULL;
}

static char *get_token_generic(char **str, char delimiter)
{
	char *c, *d;

	c = (char *)strchr(*str, delimiter);
	if(c == NULL) {
		d = *str;
		*str = *str+strlen(*str);
		return d;
	}
	*c = 0;
	d = *str;
	*str = c+1;
	return d;
}

static char *get_token(char **str)
{
	return get_token_generic(str, ' ');
}

static void reboot(void)
{
	REBOOT;
}

static void status_enable(void)
{
	wprintf("Enabling status\r\n");
	status_enabled = 1;
}

static void status_disable(void)
{
	wprintf("Disabling status\r\n");
	status_enabled = 0;
}

static void json_enable(void)
{
	wprintf("Enabling json status\r\n");
	json_enabled = 1;
}

static void json_disable(void)
{
	wprintf("Disabling json status\r\n");
	json_enabled = 0;
}

static void debug_ddr(void);

extern int hdmi_in0_d0, hdmi_in0_d1, hdmi_in0_d2;
extern int hdmi_in1_d0, hdmi_in1_d1, hdmi_in1_d2;
static void json_print(void) {
  wprintf( "{" );
  unsigned long long int nr, nw;
  unsigned long long int f;
  unsigned int rdb, wrb;
  unsigned int burstbits;

  wprintf( "\"hdmi_Rx_hres\" : %d, ", hdmi_in0_resdetection_hres_read() );
  wprintf( "\"hdmi_Rx_vres\" : %d, ", hdmi_in0_resdetection_vres_read() );
  wprintf( "\"hdmi_Rx_pixel_clock\" : %d, ", hdmi_in0_freq_value_read());

  hdmi_in0_data0_wer_update_write(1);
  hdmi_in0_data1_wer_update_write(1);
  hdmi_in0_data2_wer_update_write(1);
  wprintf("\"hdmi_Rx_phase\" : \"%d %d %d\", \"hdmi_Rx_symbol_sync\" : %d%d%d, ",
	  hdmi_in0_d0, hdmi_in0_d1, hdmi_in0_d2,
	  hdmi_in0_data0_charsync_char_synced_read(),
	  hdmi_in0_data1_charsync_char_synced_read(),
	  hdmi_in0_data2_charsync_char_synced_read());
  wprintf("\"hdmi_Rx_sync_pos\" : \"%d %d %d\", \"hdmi_Rx_symbol_errors\" : \"%d %d %d\", \"hdmi_Rx_chansyncd\" : %d, ",
	  hdmi_in0_data0_charsync_ctl_pos_read(),
	  hdmi_in0_data1_charsync_ctl_pos_read(),
	  hdmi_in0_data2_charsync_ctl_pos_read(),
	  hdmi_in0_data0_wer_value_read(),
	  hdmi_in0_data1_wer_value_read(),
	  hdmi_in0_data2_wer_value_read(),
	  hdmi_in0_chansync_channels_synced_read());
  
  wprintf( "\"overlay_hres\" : %d, ", hdmi_in1_resdetection_hres_read() );
  wprintf( "\"overlay_vres\" : %d, ", hdmi_in1_resdetection_vres_read() );
  wprintf( "\"overaly_pixel_clock\" : %d, ", hdmi_in1_freq_value_read());

  hdmi_in1_data0_wer_update_write(1);
  hdmi_in1_data1_wer_update_write(1);
  hdmi_in1_data2_wer_update_write(1);
  wprintf("\"overlay_phase\" : \"%d %d %d\", \"overlay_symbol_sync\" : %d%d%d, ",
	  hdmi_in1_d0, hdmi_in1_d1, hdmi_in1_d2,
	  hdmi_in1_data0_charsync_char_synced_read(),
	  hdmi_in1_data1_charsync_char_synced_read(),
	  hdmi_in1_data2_charsync_char_synced_read());
  wprintf("\"overlay_sync_pos\" : \"%d %d %d\", \"overlay_symbol_errors\" : \"%d %d %d\", \"overlay_chansyncd\" : %d, ",
	  hdmi_in1_data0_charsync_ctl_pos_read(),
	  hdmi_in1_data1_charsync_ctl_pos_read(),
	  hdmi_in1_data2_charsync_ctl_pos_read(),
	  hdmi_in1_data0_wer_value_read(),
	  hdmi_in1_data1_wer_value_read(),
	  hdmi_in1_data2_wer_value_read(),
	  hdmi_in1_chansync_channels_synced_read());
  
  sdram_controller_bandwidth_update_write(1);
  nr = sdram_controller_bandwidth_nreads_read();
  nw = sdram_controller_bandwidth_nwrites_read();
  f = SYSTEM_CLOCK_FREQUENCY;
  burstbits = (2*DFII_NPHASES) << DFII_PIX_DATA_SIZE;
  rdb = (nr*f >> (27 - log2(burstbits)))/1000000ULL;
  wrb = (nw*f >> (27 - log2(burstbits)))/1000000ULL;
  wprintf("\"ddr_read_Mbps\" : %d, \"ddr_write_Mbps\" : %d, ", rdb, wrb);
  
  wprintf( "\"fpga_die_temp\" : \"%dC\" ", (((unsigned int)xadc_temperature_read() * 503975) / 4096 - 273150) / 1000);
  
  wprintf( "}\n\r" );
}

static void status_print(void)
{

#ifdef CSR_HDMI_IN0_BASE
	wprintf(
		"input0:  %dx%d",
		hdmi_in0_resdetection_hres_read(),
		hdmi_in0_resdetection_vres_read());
#ifdef CSR_HDMI_IN0_FREQ_BASE
	wprintf(" (@ %3d.%2d MHz)", hdmi_in0_freq_value_read() / 1000000,
		                        (hdmi_in0_freq_value_read() / 10000) % 100);
#endif
	wprintf("\r\n");
#endif

#ifdef CSR_HDMI_IN1_BASE
	wprintf(
		"input1:  %dx%d",
		hdmi_in1_resdetection_hres_read(),
		hdmi_in1_resdetection_vres_read());
#ifdef CSR_HDMI_IN1_FREQ_BASE
	wprintf(" (@ %3d.%2d MHz)", hdmi_in1_freq_value_read() / 1000000,
		                        (hdmi_in1_freq_value_read() / 10000) % 100);
#endif
	wprintf("\r\n");
#endif
	wprintf( "xadc: %d mC\n\r", ((unsigned int)xadc_temperature_read() * 503975) / 4096 - 273150 );

#ifdef CSR_HDMI_OUT0_BASE
	unsigned int underflows;
	wprintf("output0: ");
	if(hdmi_out0_core_initiator_enable_read()) {
		hdmi_out0_core_underflow_enable_write(1);
	    hdmi_out0_core_underflow_update_write(1);
	    underflows = hdmi_out0_core_underflow_counter_read();
		wprintf(
			"%dx%d@%dHz from %s (underflows: %d)",
			processor_h_active,
			processor_v_active,
			processor_refresh,
			processor_get_source_name(processor_hdmi_out0_source),
			underflows);
		hdmi_out0_core_underflow_enable_write(0);
		hdmi_out0_core_underflow_enable_write(1);
	} else
		wprintf("off");
	wprintf("\r\n");
#endif

#ifdef CSR_HDMI_OUT1_BASE
	wprintf("output1: ");
	if(hdmi_out1_core_initiator_enable_read()) {
        hdmi_out1_core_underflow_enable_write(1);
	    hdmi_out1_core_underflow_update_write(1);
	    underflows = hdmi_out1_core_underflow_counter_read();
		wprintf(
			"%dx%d@%uHz from %s (underflows: %d)",
			processor_h_active,
			processor_v_active,
			processor_refresh,
			processor_get_source_name(processor_hdmi_out1_source),
			underflows);
		hdmi_out1_core_underflow_enable_write(0);
		hdmi_out1_core_underflow_enable_write(1);
	} else
		wprintf("off");
	wprintf("\r\n");
#endif

#ifdef ENCODER_BASE
	wprintf("encoder: ");
	if(encoder_enabled) {
		wprintf(
			"%dx%d @ %dfps from %s (q: %d)",
			processor_h_active,
			processor_v_active,
			encoder_fps,
			processor_get_source_name(processor_encoder_source),
			encoder_quality);
	} else
		wprintf("off");
	wprintf("\r\n");
#endif
#ifdef CSR_SDRAM_CONTROLLER_BANDWIDTH_UPDATE_ADDR
	wprintf("ddr: ");
	debug_ddr();
#endif
#ifdef CSR_DMA_WRITER_BASE
	wprintf("DMA_WRITER overflows: %d\n", dma_writer_overflows_read());
#endif
#ifdef CSR_DMA_READER_BASE
	wprintf("DMA_READER underflows: %d\n", dma_reader_underflows_read());
#endif
}

static void status_service(void)
{
	static int last_event;

	if(elapsed(&last_event, SYSTEM_CLOCK_FREQUENCY)) {
		if(status_enabled) {
			status_print();
			wprintf("\r\n");
		}
		if(json_enabled) {
			json_print();
			wprintf("\r\n");
		}
	}
}

// FIXME
#define HDMI_IN0_MNEMONIC ""
#define HDMI_IN1_MNEMONIC ""
#define HDMI_OUT0_MNEMONIC ""
#define HDMI_OUT1_MNEMONIC ""

#define HDMI_IN0_DESCRIPTION ""
#define HDMI_IN1_DESCRIPTION ""
#define HDMI_OUT0_DESCRIPTION ""
#define HDMI_OUT1_DESCRIPTION ""
// FIXME

static void video_matrix_list(void)
{
	wprintf("Video sources:\r\n");
#ifdef CSR_HDMI_IN0_BASE
	wprintf("input0: %s\r\n", HDMI_IN0_MNEMONIC);
	wputs(HDMI_IN0_DESCRIPTION);
#endif
#ifdef CSR_HDMI_IN1_BASE
	wprintf("input1: %s\r\n", HDMI_IN1_MNEMONIC);
	wputs(HDMI_IN1_DESCRIPTION);
#endif
	wprintf("pattern:\r\n");
	wprintf("  Video pattern\r\n");
	wputs(" ");
	wprintf("Video sinks:\r\n");
#ifdef CSR_HDMI_OUT0_BASE
	wprintf("output0: %s\r\n", HDMI_OUT0_MNEMONIC);
	wputs(HDMI_OUT0_DESCRIPTION);
#endif
#ifdef CSR_HDMI_OUT1_BASE
	wprintf("output1: %s\r\n", HDMI_OUT1_MNEMONIC);
	wputs(HDMI_OUT1_DESCRIPTION);
#endif
#ifdef ENCODER_BASE
	wprintf("encoder:\r\n");
	wprintf("  JPEG encoder (USB output)\r\n");
#endif
	wputs(" ");
}

static void video_matrix_connect(int source, int sink)
{
	if(source >= 0 && source <= VIDEO_IN_PATTERN)
	{
		if(sink >= 0 && sink <= VIDEO_OUT_HDMI_OUT1) {
			wprintf("Connecting %s to output%d\r\n", processor_get_source_name(source), sink);
			if(sink == VIDEO_OUT_HDMI_OUT0)
#ifdef CSR_HDMI_OUT0_BASE
				processor_set_hdmi_out0_source(source);
#else
				wprintf("hdmi_out0 is missing.\r\n");
#endif
			else if(sink == VIDEO_OUT_HDMI_OUT1)
#ifdef CSR_HDMI_OUT1_BASE
				processor_set_hdmi_out1_source(source);
#else
				wprintf("hdmi_out1 is missing.\r\n");
#endif
			processor_update();
		}
#ifdef ENCODER_BASE
		else if(sink == VIDEO_OUT_ENCODER) {
			wprintf("Connecting %s to encoder\r\n", processor_get_source_name(source));
			processor_set_encoder_source(source);
			processor_update();
		}
#endif
	}
}

static void video_mode_list(void)
{
	char mode_descriptors[PROCESSOR_MODE_COUNT*PROCESSOR_MODE_DESCLEN];
	int i;

	processor_list_modes(mode_descriptors);
	wprintf("Available video modes:\r\n");
	for(i=0;i<PROCESSOR_MODE_COUNT;i++)
		wprintf("mode %d: %s\r\n", i, &mode_descriptors[i*PROCESSOR_MODE_DESCLEN]);
	wprintf("\r\n");
}

static void video_mode_set(int mode)
{
	char mode_descriptors[PROCESSOR_MODE_COUNT*PROCESSOR_MODE_DESCLEN];
	if(mode < PROCESSOR_MODE_COUNT) {
		processor_list_modes(mode_descriptors);
		wprintf("Setting video mode to %s\r\n", &mode_descriptors[mode*PROCESSOR_MODE_DESCLEN]);
		config_set(CONFIG_KEY_RESOLUTION, mode);
		processor_start(mode);
	}
}

static void hdp_toggle(int source)
{
#if defined(CSR_HDMI_IN0_BASE) || defined(CSR_HDMI_IN1_BASE)
	int i;
#endif
	wprintf("Toggling HDP on output%d\r\n", source);
#ifdef CSR_HDMI_IN0_BASE
	if(source ==  VIDEO_IN_HDMI_IN0) {
		hdmi_in0_edid_hpd_en_write(0);
		for(i=0; i<65536; i++);
		hdmi_in0_edid_hpd_en_write(1);
	}
#else
	wprintf("hdmi_in0 is missing.\r\n");
#endif
#ifdef CSR_HDMI_IN1_BASE
	if(source == VIDEO_IN_HDMI_IN1) {
		hdmi_in1_edid_hpd_en_write(0);
		for(i=0; i<65536; i++);
		hdmi_in1_edid_hpd_en_write(1);
	}
#else
	wprintf("hdmi_in1 is missing.\r\n");
#endif
}

#ifdef CSR_HDMI_OUT0_BASE
static void output0_on(void)
{
	wprintf("Enabling output0\r\n");
	hdmi_out0_core_initiator_enable_write(1);
}

static void output0_off(void)
{
	wprintf("Disabling output0\r\n");
	hdmi_out0_core_initiator_enable_write(0);
}
#endif

#ifdef CSR_HDMI_OUT1_BASE
static void output1_on(void)
{
	wprintf("Enabling output1\r\n");
	hdmi_out1_core_initiator_enable_write(1);
}

static void output1_off(void)
{
	wprintf("Disabling output1\r\n");
	hdmi_out1_core_initiator_enable_write(0);
}
#endif

#ifdef ENCODER_BASE
static void encoder_on(void)
{
	wprintf("Enabling encoder\r\n");
	encoder_enable(1);
}

static void encoder_configure_quality(int quality)
{
	wprintf("Setting encoder quality to %d\r\n", quality);
	encoder_set_quality(quality);
}

static void encoder_configure_fps(int fps)
{
	wprintf("Setting encoder fps to %d\r\n", fps);
	encoder_set_fps(fps);
}

static void encoder_off(void)
{
	wprintf("Disabling encoder\r\n");
	encoder_enable(0);
}
#endif

static void debug_mmcm(void)
{
	mmcm_dump();
}

#ifdef CSR_SDRAM_CONTROLLER_BANDWIDTH_UPDATE_ADDR
static void debug_ddr(void)
{
	unsigned long long int nr, nw;
	unsigned long long int f;
	unsigned int rdb, wrb;
	unsigned int burstbits;

	sdram_controller_bandwidth_update_write(1);
	nr = sdram_controller_bandwidth_nreads_read();
	nw = sdram_controller_bandwidth_nwrites_read();
	f = SYSTEM_CLOCK_FREQUENCY;
	burstbits = (2*DFII_NPHASES) << DFII_PIX_DATA_SIZE;
	rdb = (nr*f >> (27 - log2(burstbits)))/1000000ULL;
	wrb = (nw*f >> (27 - log2(burstbits)))/1000000ULL;
	wprintf("read:%5dMbps  write:%5dMbps  all:%5dMbps\r\n", rdb, wrb, rdb + wrb);
}
#endif

#ifdef CSR_DNA_ID_ADDR
static void print_board_dna(void) {
	int i;
	wprintf("Board's DNA: ");
	for(i=0; i<CSR_DNA_ID_SIZE; i++) {
		wprintf("%02x", MMPTR(CSR_DNA_ID_ADDR+4*i));
	}
	wprintf("\n");
}

#endif

void ci_prompt(void)
{
	wprintf("RUNTIME>");
}

void init_rect(int mode);

void init_rect(int mode) {
  const struct video_timing *m = &video_modes[mode];

  hdmi_core_out0_initiator_enable_write(0);

  hdmi_core_out0_initiator_base_write(hdmi_in1_framebuffer_base(hdmi_in1_fb_index));
		  
  hdmi_core_out0_initiator_hres_write(m->h_active);
  hdmi_core_out0_initiator_hsync_start_write(m->h_active + m->h_sync_offset);
  hdmi_core_out0_initiator_hsync_end_write(m->h_active + m->h_sync_offset + m->h_sync_width);
  hdmi_core_out0_initiator_hscan_write(m->h_active + m->h_blanking - 1);
  hdmi_core_out0_initiator_vres_write(m->v_active);
  hdmi_core_out0_initiator_vsync_start_write(m->v_active + m->v_sync_offset);
  hdmi_core_out0_initiator_vsync_end_write(m->v_active + m->v_sync_offset + m->v_sync_width);
  hdmi_core_out0_initiator_vscan_write(m->v_active + m->v_blanking);
  
  hdmi_core_out0_initiator_length_write(m->h_active*m->v_active*4);

  int h_margin = 10;
  int v_margin = 10;
  int rect_thresh = 8; // reasonable for magic mirror use, which is mostly a black UI
  rectangle_hrect_start_write(h_margin);
  rectangle_hrect_end_write(m->h_active - h_margin);
  rectangle_vrect_start_write(v_margin);
  rectangle_vrect_end_write(m->v_active - v_margin);
  rectangle_rect_thresh_write(8);
  
  hdmi_core_out0_dma_delay_base_write(120);  // this helps align the DMA transfer through various delay offsets
  // empricially determined, will shift around depending on what you do in the overlay video pipe, e.g.
  // ycrcb422 vs rgb
  
  hdmi_core_out0_initiator_enable_write(1);
}

static int ci_iters = 0;
void ci_service(void)
{
	char *str;
	char *token;
	char dummy[] = "dummy";
	int was_dummy = 0;
	
	status_service();

	str = readstr();
	if(str == NULL) {
	  str = (char *) dummy;
	}

	ci_iters++;
	token = get_token(&str);

	if(strncmp(token, "dummy", 5) == 0) {
	  was_dummy = 1;
	} else if(strcmp(token, "help") == 0) {
		wputs("Available commands:");
		token = get_token(&str);
		if(strcmp(token, "video_matrix") == 0)
			help_video_matrix();
		else if(strcmp(token, "video_mode") == 0)
			help_video_mode();
		else if(strcmp(token, "hdp_toggle") == 0)
			help_hdp_toggle();
#ifdef CSR_HDMI_OUT0_BASE
		else if(strcmp(token, "output0") == 0)
			help_output0();
#endif
#ifdef CSR_HDMI_OUT1_BASE
		else if(strcmp(token, "output1") == 0)
			help_output1();
#endif
#ifdef ENCODER_BASE
		else if(strcmp(token, "encoder") == 0)
			help_encoder();
#endif
		else if(strcmp(token, "debug") == 0)
			help_debug();
		else
			ci_help();
		wputs("");
	} else if(strcmp(token, "reboot") == 0) reboot();
	else if(strcmp(token, "mr") == 0) mr(get_token(&str), get_token(&str));
	else if(strcmp(token, "mw") == 0) mw(get_token(&str), get_token(&str), get_token(&str));
	else if(strcmp(token, "mc") == 0) mc(get_token(&str), get_token(&str), get_token(&str));
	else if(strcmp(token, "video_matrix") == 0) {
		token = get_token(&str);
		if(strcmp(token, "list") == 0) {
			video_matrix_list();
		}
		else if(strcmp(token, "connect") == 0) {
			int source;
			int sink;
			/* get video source */
			token = get_token(&str);
			source = -1;
			if(strcmp(token, "input0") == 0) {
				source = VIDEO_IN_HDMI_IN0;
			}
			else if(strcmp(token, "input1") == 0) {
				source = VIDEO_IN_HDMI_IN1;
			}
			else if(strcmp(token, "pattern") == 0) {
				source = VIDEO_IN_PATTERN;
			}
			else {
				wprintf("Unknown video source: '%s'\r\n", token);
			}

			/* get video sink */
			token = get_token(&str);
			sink = -1;
			if(strcmp(token, "output0") == 0) {
				sink = VIDEO_OUT_HDMI_OUT0;
			}
			else if(strcmp(token, "output1") == 0) {
				sink = VIDEO_OUT_HDMI_OUT1;
			}
			else if(strcmp(token, "encoder") == 0) {
				sink = VIDEO_OUT_ENCODER;
			}
			else
				wprintf("Unknown video sink: '%s'\r\n", token);

			if (source >= 0 && sink >= 0)
				video_matrix_connect(source, sink);
			else
				help_video_matrix();
		} else {
			help_video_matrix();
		}
	}
	else if(strcmp(token, "video_mode") == 0) {
		token = get_token(&str);
		if(strcmp(token, "list") == 0)
			video_mode_list();
		else
			video_mode_set(atoi(token));
	}
	else if(strcmp(token, "hdp_toggle") == 0) {
		token = get_token(&str);
		hdp_toggle(atoi(token));
	}
#ifdef CSR_HDMI_OUT0_BASE
	else if(strcmp(token, "output0") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			output0_on();
		else if(strcmp(token, "off") == 0)
			output0_off();
		else
			help_output0();
	}
#endif
#ifdef CSR_HDMI_OUT1_BASE
	else if(strcmp(token, "output1") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			output1_on();
		else if(strcmp(token, "off") == 0)
			output1_off();
		else
			help_output1();
	}
#endif
#ifdef ENCODER_BASE
	else if(strcmp(token, "encoder") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			encoder_on();
		else if(strcmp(token, "off") == 0)
			encoder_off();
		else if(strcmp(token, "quality") == 0)
			encoder_configure_quality(atoi(get_token(&str)));
		else if(strcmp(token, "fps") == 0)
			encoder_configure_fps(atoi(get_token(&str)));
		else
			help_encoder();
	}
#endif
#ifdef CSR_GENERATOR_BASE
	else if(strcmp(token, "sdram_test") == 0) {
	  bist_test();
	}
#endif
#ifdef CSR_DMA_WRITER_BASE
	else if(strcmp(token, "dma_writer") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0) {
			dma_writer_enable_write(1);
			dma_writer_start_write(1);
			wprintf("dma_writer on\n");
		} else if(strcmp(token, "off") == 0) {
			dma_writer_enable_write(0);
			dma_writer_start_write(0);
			wprintf("dma_writer off\n");
		}
	}
#endif
#ifdef CSR_DMA_READER_BASE
	else if(strcmp(token, "dma_reader") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0) {
			dma_reader_enable_write(1);
			dma_reader_start_write(1);
			wprintf("dma_reader on\n");
		} else if(strcmp(token, "off") == 0) {
			dma_reader_enable_write(0);
			dma_reader_start_write(0);
			wprintf("dma_reader off\n");
		}
	}
#endif

	else if(strcmp(token, "status") == 0) {
		token = get_token(&str);
		if(strcmp(token, "on") == 0)
			status_enable();
		else if(strcmp(token, "off") == 0)
			status_disable();
		else
			status_print();
	}

	else if(strcmp(token, "json") == 0) {
	  token = get_token(&str);
	  if(strcmp(token, "on") == 0)
	    json_enable();
	  else if(strcmp(token, "off") == 0)
	    json_disable();
	  else
	    json_print();
	}
	else if(strcmp(token, "debug") == 0) {
		token = get_token(&str);
		if(strcmp(token, "mmcm") == 0)
			debug_mmcm();
#ifdef CSR_HDMI_IN0_BASE
		else if(strcmp(token, "input0") == 0) {
			hdmi_in0_debug = !hdmi_in0_debug;
			wprintf("HDMI Input 0 debug %s\r\n", hdmi_in0_debug ? "on" : "off");
		}
#endif
#ifdef CSR_HDMI_IN1_BASE
		else if(strcmp(token, "input1") == 0) {
			hdmi_in1_debug = !hdmi_in1_debug;
			wprintf("HDMI Input 1 debug %s\r\n", hdmi_in1_debug ? "on" : "off");
		}
#endif
#ifdef CSR_SDRAM_CONTROLLER_BANDWIDTH_UPDATE_ADDR
		else if(strcmp(token, "ddr") == 0)
			debug_ddr();
#endif
#ifdef CSR_DNA_ID_ADDR
		else if(strcmp(token, "dna") == 0)
			print_board_dna();
#endif
		else if(strcmp(token, "edid") == 0) {
			unsigned int found = 0;
			token = get_token(&str);
#ifdef CSR_HDMI_OUT0_I2C_W_ADDR
			if(strcmp(token, "output0") == 0) {
				found = 1;
				hdmi_out0_print_edid();
			}
#endif
#ifdef CSR_HDMI_OUT1_I2C_W_ADDR
			if(strcmp(token, "output1") == 0) {
				found = 1;
				hdmi_out1_print_edid();
			}
#endif
			if(found == 0)
				wprintf("%s port has no EDID capabilities\r\n", token);
		} else if(strcmp(token, "rect") == 0 ) {
		  init_rect(config_get(CONFIG_KEY_RESOLUTION));
		} else if(strcmp(token, "setrect") == 0 ) {
		  const struct video_timing *m = &video_modes[12];
		  m = &video_modes[12];
		  
		  rectangle_hrect_start_write((unsigned short) strtoul(get_token(&str), NULL, 0));
		  rectangle_hrect_end_write((unsigned short) strtoul(get_token(&str), NULL, 0));
		  // vblank on top of frame so compensate in offset
		  rectangle_vrect_start_write((unsigned short) strtoul(get_token(&str), NULL, 0) + m->v_blanking ); 
		  rectangle_vrect_end_write((unsigned short) strtoul(get_token(&str), NULL, 0) + m->v_blanking );
		} else if(strcmp(token, "rectoff") == 0 ) {
		  hdmi_core_out0_initiator_enable_write(0);
		} else if (strcmp(token, "delay") == 0) {
		  hdmi_core_out0_dma_delay_base_write((unsigned int) strtoul(get_token(&str), NULL, 0));
		  wprintf("delay value: %d\r\n", hdmi_core_out0_dma_delay_base_read());
		} else if( strcmp(token, "rectthresh") == 0) {
		  rectangle_rect_thresh_write((unsigned short) strtoul(get_token(&str), NULL, 0)); 
		} else if( strcmp(token, "xadc") == 0) {
		  wprintf( "xadc: %d mC\n", ((unsigned int)xadc_temperature_read() * 503975) / 4096 - 273150 );
		} else if( strcmp(token, "km") == 0 ) {
		  derive_km();
		} else if( strcmp(token, "hpdforce") == 0 ) {
		  hdcp_hpd_ena_write(1);
		} else if( strcmp(token, "hpdrelax") == 0 ) {
		  hdcp_hpd_ena_write(0);
		} else if( strcmp(token, "dumpe") == 0 ) {
		  int i ;
		  for( i = 0; i < 256; i++ ) {
		    if( (i % 16) == 0 ) {
		      wprintf( "\r\n %02x: ", i );
		    }
		    i2c_snoop_edid_snoop_adr_write( i );
		    // may need to add a delay to allow write->read access time
		    wprintf( "%02x ", i2c_snoop_edid_snoop_dat_read() );
		  }
		} else if( strcmp(token, "inc") == 0 ) {
#if 0
		  int run = 0;
		  flush_cpu_icache();
		  flush_cpu_dcache();
		  hdmi_in1_dma_address_valid_write(1);
		  hdmi_in1_dma_dma_go_write(1);
#else
		  printf( "not debugging dma\n" );
#endif
#if 0
		  while(hdmi_in1_dma_dma_running_read()) {
		    wprintf( "run%d ", run++ );
		    flush_cpu_icache();
		    flush_cpu_dcache();
		  }
#endif
		} else if (strcmp(token, "dvimode0") == 0 ) {
		  hdmi_in0_decode_terc4_dvimode_write(1);
		} else if (strcmp(token, "hdmimode0") == 0 ) {
		  hdmi_in0_decode_terc4_dvimode_write(0);
		} else if (strcmp(token, "dvimode1") == 0 ) {
		  hdmi_in1_decode_terc4_dvimode_write(1);
		} else if (strcmp(token, "hdmimode1") == 0 ) {
		  hdmi_in1_decode_terc4_dvimode_write(0);
		} else if (strcmp(token, "stop") == 0 ) {
		  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_EMPTY);
		  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_EMPTY);
		} else if (strcmp(token, "run") == 0 ) {
		  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);
		  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
		} else {
		  help_debug();
		}
	} else {
	  //		if(status_enabled)
	  //			status_disable();
	}
	if( !was_dummy ) {
	  ci_prompt();
	}
}

/*
		else if(strcmp(token, "dma") == 0 ) {
		wprintf("initiating DMA on HDMI1\r\n");
		  hdmi_in1_dma_ev_enable_write(0x3);
		}

 else if( strcmp(token, "inc" == 0 ) ) {
		  int hres = 1920;
		  int vres = 1080;
		  unsigned int mask;
		  
		  hdmi_in1_dma_frame_size_write(hres*vres*4);
		  hdmi_in1_fb_slot_indexes[0] = 0;
		  hdmi_in1_dma_slot0_address_write(hdmi_in1_framebuffer_base(0));
		  printf( "slot0 %x\n", hdmi_in1_framebuffer_base(0) );
		  hdmi_in1_dma_slot0_status_write(DVISAMPLER_SLOT_LOADED);

#if 0
		  hdmi_in1_fb_slot_indexes[1] = 1;
		  hdmi_in1_dma_slot1_address_write(hdmi_in1_framebuffer_base(1));
		  printf( "slot1 %x\n", hdmi_in1_framebuffer_base(1) );
		  hdmi_in1_dma_slot1_status_write(DVISAMPLER_SLOT_LOADED);
		  hdmi_in1_next_fb_index = 2;
#endif
		  
		  hdmi_in1_dma_ev_pending_write(hdmi_in1_dma_ev_pending_read());
		  //		  hdmi_in1_dma_ev_enable_write(0x3);
		  hdmi_in1_dma_ev_enable_write(0x1);

		  mask = irq_getmask();
		  mask |= 1 << HDMI_IN1_INTERRUPT;
		  printf( "irq mask: %x\n", mask );
		  irq_setmask(mask);
		}
*/
