/*************************************************************//**** slice~.c - Nao Tokui, nao1091@mac.com, Jan 2005 ********//*************************************************************/#include "ext.h"#include "ext_common.h" // contains CLIP macro#include "z_dsp.h"#include "ext_mess.h"#include "buffer.h"//#define SAMPLE_INTERVAL 5000// better set this is in milliseconds to be independant of sampling rate#define SAMPLE_INTERVAL 113.4	// vb, in milliseconds#define DEF_RMS_COEF 0.25#define DEF_THRESH  0.09#define DEF_LOW_THRESH 0.08#define LOOK_BACK 20				// vb, in milliseconds#define LOADMESSAGE "slice~ by nao tokui :: nao1091@mac.com"#define VERSION "slice~ version 1.1.1, Jul 2015 -- additional tweaks by volker böhm"//#define DEFAULT_MAX_SEGMENT_NUM 256void *slice_class;/* definition of sliceg~ object */typedef struct _slice{	t_pxobject l_obj;	t_symbol *l_sym; 	// name of target buffer~	t_buffer *l_buf;   	// pointer to the buffer~	long l_chan;		// (target channel) - 1	void *l_outlet1;	// list outlet (start and end points of segments)	void *l_outlet2;	// the number of segments	void *l_outlet3;	// bang out	int l_malloced; 	// flag for "Is already malloced?"	long frames;		//  stored current size of buffer~	float *tab_env,*tab_hp;	double rms_coef;  // to calcurate RMS	double thresh;		// Threshold to decide "attack" or not.	double	sample_interval, look_back;	//   long **segments; // start and end point of each segment 	int segment_num; 		// number of segment (1 - MAX_SEGMENT_NUM)	//  int current_max_segment;	/**** temporaly value ***/	long th_point; 	// Attack point (samples) } t_slice;void slice_set(t_slice *x, t_symbol *s);void *slice_new(t_symbol *s, long chan);void slice_float(t_slice *x, float coef);void slice_bang(t_slice *x);//void slice_int(t_slice *x, long segment);void slice_ft1(t_slice *x, float f);void slice_int2(t_slice *x, long n);// vb, some additional functions for further tweakingvoid slice_setInterval(t_slice *x, double f);void slice_lookback(t_slice *x, double f);void slice_assist(t_slice *x, void *b, long m, long a, char *s);void slice_dblclick(t_slice *x);void slice_bang_do (t_slice *x, t_symbol *s, short argc, t_atom *argv);void slice_bangout_do(t_slice *x, t_symbol *s, short argc, t_atom *argv);//void slice_bangout_do2(t_slice *x, t_symbol *s, short argc, t_atom *argv);//void slice_listout_do(t_slice *x, t_symbol *s, short argc, t_atom *argv);void slice_malloc(t_slice *x);long find_zerocross(long a, long b,float *table, long tab_size);float table_complement(float *tab,float f_idx);//int sign(float value);t_symbol *ps_buffer;/* main function */int C74_EXPORT main(void){	t_class *c;		c = class_new("slice~", (method)slice_new, 0L, (short)sizeof(t_slice), 0L, A_SYM, A_DEFLONG, 0L);		/* addmess */	class_addmethod(c, (method)slice_set, "set", A_SYM, 0);			// "set" message changes target buffer~	class_addmethod(c, (method)slice_float, "float", A_FLOAT, 0);	// set rms feedback coef					class_addmethod(c, (method)slice_bang, "bang", 0);				// start splicing	class_addmethod(c, (method)slice_ft1,"ft1", A_FLOAT, 0);      // set threshold to determin 'attack' or not.	class_addmethod(c, (method)slice_int2, "in2", A_LONG, 0);	// set the number of desired slices // vb, inactive		class_addmethod(c, (method)slice_setInterval, "interval", A_FLOAT, 0);	// 	class_addmethod(c, (method)slice_lookback, "lookback", A_FLOAT, 0);	// 		class_addmethod(c, (method)slice_assist, "assist", A_CANT, 0); 		// help	class_addmethod(c, (method)slice_dblclick, "dblclick", A_CANT, 0); 	// double click to see buffer~ contents			ps_buffer = gensym("buffer~"); 		post(LOADMESSAGE); // display message and version in the Max Window.	post(VERSION);    	class_register(CLASS_BOX, c);	slice_class = c;		return 0;}/* "set" message changes target buffer~ */void slice_set(t_slice *x, t_symbol *s) {	t_buffer *b;		x->l_sym = s;	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer) {		x->l_buf = b;	} else {		object_error((t_object *)x, "slice~: no buffer~ %s", s->s_name);		x->l_buf = 0;	}}/* malloc required memory */void slice_malloc(t_slice *x){	if (x->l_malloced==false && x->l_buf!=NULL){ // allocate memory for tables used in cutting		x->tab_env = (float*)sysmem_newptr(sizeof(float)*x->frames);		x->tab_hp = (float*)sysmem_newptr(sizeof(float)*x->frames);			if (x->tab_env==NULL||x->tab_hp==NULL) {			object_error((t_object *)x, "slice~: memory allocation error");			return;		} else			x->l_malloced = true;	}}/* changing feedback coefficient by float number in right inlet */// vb, that should probably read "left inlet"void slice_float(t_slice *x, float coef)  {	x->rms_coef = coef;//	post("rms feedback coef: %f",x->thresh);}/* bang start splicing (left most inlet) */void slice_bang(t_slice *x)  {  x->segment_num=0;    defer(x, (method)slice_bang_do, 0L, 0, 0L);}/*********************************************************************//***************   SLICING FUNCTION                *******************//*********************************************************************/void slice_bang_do (t_slice *x, t_symbol *s, short argc, t_atom *argv){	t_buffer *b; // target buffer~  	float *tab;	long idx0,idx1,chan, nc;	double xx;				double fb,fb2;					float coef, maxv, sr;	t_atom outList[3];	long lb_samp, samp_interv;	long start_p, end_p;			/* set target buffer~ */	slice_set(x, x->l_sym);	  	b = x->l_buf;	/* exception handling */	if (!b || !(b->b_valid)) {		object_error((t_object *)x, "slice~: invalid buffer~ %s", x->l_sym->s_name);		return;	}			tab = b->b_samples; 	// sample table in buffer~	x->frames = b->b_frames; 	chan = x->l_chan; 		// channel to scan	nc = b->b_nchans; 		// number of channels in buffer~	sr = x->l_buf->b_sr;	fb = 0.0;				// value of sample to feedback	fb2 = 0.0;	x->segment_num = 0;	// number of segments		/* memory allocation */	slice_malloc(x);	if (x->l_malloced==false) return;	coef = x->rms_coef;			// feedback coefficient	idx0 = 0;					// frame index to scan//	x->th_point=0-SAMPLE_INTERVAL;	x->th_point=1;	maxv = 0.0;		/* making envelope table */	while (idx0 < x->frames){		idx1 = idx0*nc + chan;			// index converted		xx = tab[idx1];		xx = xx * xx;					// square 				xx = (xx * (1 - coef)) + (fb * coef);	// filter		fb = xx;						// store feedback value		xx = sqrt(xx);					// square root		x->tab_env[idx0] = xx;		idx0++; 	}		/* find maximum amplitude in the buffer */	for (idx0=0;idx0<x->frames;idx0++){		if (maxv<x->tab_env[idx0]) maxv = x->tab_env[idx0];	}		/* normalization of amplitude */	if (maxv!=0.0){		for (idx0=0;idx0<x->frames;idx0++){			x->tab_env[idx0]=x->tab_env[idx0]/maxv;		}	}		/* making buffer table after rms+high pass filter */ 	for (idx0=0;idx0<x->frames;idx0++){		xx = x->tab_env[idx0];		xx = (1.3*xx - 0.9*fb2)*(1.3*xx - 0.9*fb2);		x->tab_hp[idx0]=xx;		fb2 = xx;	}	/* splicing and output in list of start and end point of each segment */ 	idx0 = 0;	lb_samp = x->look_back*x->l_buf->b_sr/1000; 	samp_interv = x->sample_interval * sr/1000;		while (idx0 < x->frames){		/*if (x->segment_num==0 && x->tab_hp[idx0] > DEF_LOW_THRESH) 			x->th_point=find_zerocross(idx0-LOOK_BACK,idx0,x->tab_hp);*/				if (idx0 <samp_interv && x->tab_hp[idx0] > DEF_LOW_THRESH 			&& idx0-x->th_point > samp_interv) {				x->th_point=find_zerocross(idx0-lb_samp,idx0,x->tab_hp, x->frames);		}		else if ((x->tab_hp[idx0-1]<DEF_LOW_THRESH&&x->tab_hp[idx0]>x->thresh&&idx0-x->th_point>samp_interv)			|| idx0==x->frames-1) {						if (x->th_point>0){								start_p =x->th_point; // start point in samples				end_p =find_zerocross(idx0-lb_samp,idx0,x->tab_hp,x->frames); // end point in samples								/****** checking the number of segments and storing the segment data 				if ((++(x->segment_num))< x->current_max_segment) {					x->segments[x->segment_num][0] = start_p; 					x->segments[x->segment_num][1] = end_p; 				} else {					error("too many segments...");					break;				}******/								(x->segment_num)++;								atom_setlong(&outList[0],x->segment_num);				atom_setfloat(&outList[1],((float)start_p/sr)*1000.0);	// start point in ms  // vb, get rid of "+1" and change to float				atom_setfloat(&outList[2], ((float)end_p/sr)*1000.0);	// end point in ms				outlet_list(x->l_outlet1,0L,3,outList); 			// list output 				 				x->th_point  = end_p;			} 		}		idx0++;	}		/* free allocated memory */ 	if (x->l_malloced){		// free memory for buffer		sysmem_freeptr(x->tab_hp);		sysmem_freeptr(x->tab_env);		x->l_malloced=false;	}	// bang when finished	outlet_int(x->l_outlet2, x->segment_num);	outlet_bang(x->l_outlet3);  	//defer(x, (method)slice_bangout_do, 0L, 0, 0L);}/** reallocate array and move data to it void slice_reallocate(){}**//* output bang when splicing is finished *//*void slice_bangout_do(t_slice *x, t_symbol *s, short argc, t_atom *argv){	outlet_int(x->l_outlet2, x->segment_num);	outlet_bang(x->l_outlet3);}*//* output bang when splicing is finished *//*void slice_bangout_do2(t_slice *x, t_symbol *s, short argc, t_atom *argv){	outlet_bang(x->l_outlet3);}*//* defered list output *//*void slice_listout_do(t_slice *x, t_symbol *s, short argc, t_atom *argv){		outlet_list(x->l_outlet1,0L,argc,argv); 	}*//* output slice information *//*void slice_int(t_slice *x, long segment)  {  	t_atom outList[3];	   	if (x->segment_num>0 &&  segment >0 && segment <= x->segment_num){	  	SETLONG(&outList[0],segment);  		SETLONG(&outList[1],(x->segments[segment][0]/x->l_buf->b_sr)*1000+1);	// start point in ms 				SETLONG(&outList[2],(x->segments[segment][1]/x->l_buf->b_sr)*1000); 	// end point in ms		outlet_list(x->l_outlet1,0L,3,outList); 			  	}}*//* find (quasi) zero crossing point (i.e., minimum value) */long find_zerocross(long a, long b,float *table,long tab_size){	long min;	float minv;	long i;		min = a;	minv=1.0;	for (i=a;i<b;i++){		if (i>=0 && i<tab_size){			if (table[i]<minv) {				minv=table[i];				min = i;			}		}	}	return min;}/* set threshold to decide "attack" or not*/void slice_ft1(t_slice *x, float f) {	if (f!=0.0) x->thresh = f / 10.0;//	post("threshold: %f",x->thresh);}void slice_int2(t_slice *x, long n){	// vb, does nothing	//return;}void slice_setInterval(t_slice *x, double f) {	x->sample_interval = CLAMP(f, 1, f);}void slice_lookback(t_slice *x, double f) {	x->look_back = CLAMP(f, 1, f);}/* double click to show sound wave */void slice_dblclick(t_slice *x) {	t_buffer *b;		if ((b = (t_buffer *)(x->l_sym->s_thing)) && ob_sym(b) == ps_buffer)		mess0((t_object *)b,gensym("dblclick"));}/* to display assistant message */// create assistance stringsvoid slice_assist(t_slice *x, void *b, long m, long a, char *s) {	if (m==ASSIST_INLET) {		switch(a) {			case 0: sprintf (s,"(bang) start segmentation"); break;			case 1: sprintf (s,"(float) threshold"); break;			case 2: sprintf (s,"(int) inactive"); break;		}	}	else {		switch(a) {			case 0: sprintf (s,"(list) index, start point, end point"); break;			case 1: sprintf(s, "(int) number of slices"); break;			case 2: sprintf(s, "(bang) bang when finished"); break;		}	}}/* generate new instance */void *slice_new(t_symbol *s, long chan) {	//int i;	t_slice *x = object_alloc(slice_class);	if(x) {								// add inlets (3 in total)		intin((t_object *)x, 2); 		floatin((t_object *)x,1);																// generate outlet (right to left)		x->l_outlet3=bangout((t_object *)x);		x->l_outlet2=intout((t_object *)x);		x->l_outlet1 = listout((t_object *)x);											x->l_sym = s;					// name of target buffer~			if (chan) x->l_chan = CLIP_ASSIGN(chan,1,x->l_buf->b_nchans) - 1;									// l_chan: (target channel) - 1		else x->l_chan = 0;					//	x->segments = t_getbytes(sizeof(long)*(DEFAULT_MAX_SEGMENT_NUM+1)*2);				x->rms_coef = DEF_RMS_COEF;	// default feedback coef.		x->thresh = DEF_THRESH;		x->sample_interval = SAMPLE_INTERVAL;	// vb, initialize		x->look_back = LOOK_BACK;				// vb, initialize		x->segment_num = 0;		x->l_malloced = false;	   // x->current_max_segment = DEFAULT_MAX_SEGMENT_NUM;	}	else {		object_free(x);		x = NULL;	}	return (x);	}int sign(float value){	if (value>=0) return 1;	else return -1;}