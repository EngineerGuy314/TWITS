//to compile:    gcc twits.c -o twits.exe -lcurl
//args: callsign, U4B_type_channel, comment, details, [ExtendedPrecisionTelemetry-enable]
// for debug compile:   gcc -fsanitize=address -g twits.c -o twits.exe -lcurl
//gcc -fsanitize=address -g twits_bloat.c -o twits.exe -lcurl
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>
#include <unistd.h> 
#include <sys/time.h>  
#include <sys/file.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>  

#define MAX_RESPONSE_SIZE 4096

// This struct holds the buffer and current write position
struct MemoryBuffer {
    char *data;
    size_t size;
    size_t max_size;
};

char LOGFILE_PATH[256];

time_t epoch_time;
char query_raw[450];           //will cause segflt if too small (should prolly make dynamic)
char query_stringified[450];
FILE *fp;
FILE *log_file;
CURL *curl;
CURLcode res;
char site_response[2000];
char detail_prepend_msg[100];
char _4chargrid[5];
char _telem_callsign[7];
int _telem_power;
char _telem_grid[5];
int altitude;
int packet_count=0;
char grid5;
char grid6;
int grid7; //base 10 //last 4 only used for extended telem of special type
int grid8; //base 10 
int grid9; //base 24
int grid10; //base 24
int mins_since_boot;
int mins_since_lock;
char _6_char_grid[7]; 
double lat, lon;
char callsign[7];
char payload_suffix[6];
char _uploader[12];  
char detail[120];
char comment[120];
int basic_telem_bit;
int bit1;
int _knots;
int _volts;
int _temp;
int was_spinlocked=0;
int fd;
char _start_minute[2];
char _id1[2];
char _id3[2];
int _freq_lane;
int _extended_telem;
int start_minute_of_packet;
int _1st_pak_found;
int _2nd_pak_found;
int _3rd_pak_found;
int _freq;
int low_freq_limit;
int high_freq_limit;

#define SECONDS_TO_LOOK_BACK 600

// Callback that writes into the buffer
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    struct MemoryBuffer *mem = (struct MemoryBuffer *)userp;

    if (mem->size + total_size >= mem->max_size) {
        // Prevent buffer overflow
        total_size = mem->max_size - mem->size - 1;
    }

    memcpy(&(mem->data[mem->size]), contents, total_size);
    mem->size += total_size;
    mem->data[mem->size] = 0; // Null-terminate

    return total_size;
}
////********************************
void get_iso_utc_time(char *buffer, size_t size) {
    struct timeval tv;struct tm *tm_info;gettimeofday(&tv, NULL);tm_info = gmtime(&tv.tv_sec);strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", tm_info);snprintf(buffer + 19, size - 19, ".%03ldZ", tv.tv_usec / 1000); // Add milliseconds
}
void get_iso_utc_timeSIMPLE(char *buffer, size_t size) {
    struct timeval tv;struct tm *tm_info;gettimeofday(&tv, NULL);tm_info = gmtime(&tv.tv_sec);strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
//***************************************************************************
void init(int argcc, const char *arg) {
	
						//printf("about to check for spinlock\n");
	const char *lockfile = "/tmp/mylockfile.lock";              //spin lock if called more than once
    fd = open(lockfile, O_CREAT | O_RDWR, 0666);
    // Spinlock: keep trying to get the lock
    while (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        // Lock is held by another process; sleep briefly to avoid busy wait
		was_spinlocked=1;
        usleep(100000); // 100 ms
    }
    if (argcc>3)                                                               
		snprintf(LOGFILE_PATH, sizeof(LOGFILE_PATH), "twits_log_ch_%s.txt", arg);  //create a unique logfile for that channel
	else
		snprintf(LOGFILE_PATH, sizeof(LOGFILE_PATH), "twits_log_NO_CHAN.txt");
	

    struct stat st;                                        //tidy up logfile if its too big
    // Check if logfile exists and its size
    if (stat(LOGFILE_PATH, &st) == 0) {
        if (st.st_size >= 4000000) {  //max 4MB logfile
            if (remove(LOGFILE_PATH) != 0) {
				printf("spinlock delete error\n");}}}
	
	log_file = fopen(LOGFILE_PATH,"a");  
	_1st_pak_found=0;_2nd_pak_found=0;_3rd_pak_found=0;
	
								char datetime[30];
								get_iso_utc_timeSIMPLE(datetime, sizeof(datetime));
								fprintf(log_file,"%s\n",datetime);	
								
	detail_prepend_msg[0]=0;
}
//************************************************************

void maidenhead_to_latlon(const char *grid, double *lat, double *lon) {
    // Convert Maidenhead grid to lat/lon
    int field_lon = toupper(grid[0]) - 'A';
    int field_lat = toupper(grid[1]) - 'A';
    int square_lon = grid[2] - '0';
    int square_lat = grid[3] - '0';
    int subsquare_lon = toupper(grid[4]) - 'A';
    int subsquare_lat = toupper(grid[5]) - 'A';
    // Compute latitude and longitude
    *lon = (field_lon * 20.0) + (square_lon * 2.0) + ((subsquare_lon * (2.0 / 24.0))+( (1.0/24.0))) - 180.0;
    *lat = (field_lat * 10.0) + (square_lat * 1.0) + (subsquare_lat * (1.0 / 24.0))+(1.0/48.0)  - 90.0;
}

//************************************************************

void ten_char_maidenhead_to_latlon(const char *grid, int c7, int c8, int c9, int c10, double *lat, double *lon) {
    // Convert Maidenhead grid to lat/lon
    int field_lon = toupper(grid[0]) - 'A';
    int field_lat = toupper(grid[1]) - 'A';
    int square_lon = grid[2] - '0';
    int square_lat = grid[3] - '0';
    int subsquare_lon = toupper(grid[4]) - 'A';
    int subsquare_lat = toupper(grid[5]) - 'A';
    // Compute latitude and longitude
    *lon = (field_lon * 20.0) + (square_lon * 2.0) + (subsquare_lon * (2.0 / 24.0))   + (c7*(2/240.0)) + (c9*(2/(240.0*24.0)))  +(1/(240.0*24.0))     - 180.0;
    *lat = (field_lat * 10.0) + (square_lat * 1.0) + (subsquare_lat * (1.0 / 24.0))   + (c8*(1/240.0)) + (c10*(1/(240.0*24.0)))  +(1/(240.0*48.0))     - 90.0;
}

//****************************************************************************************
void replace_spaces(const char *input, char *output) {
    while (*input) {
        if (*input == ' ') {
            *output++ = '%';
            *output++ = '2';
            *output++ = '0';
        } else {
            *output++ = *input;
        }
        input++;
    }
    *output = '\0';  // Null-terminate the string
}
//******************************************************************************

void send_SQL_query(void)  //formats and sends (as a simple HTTP request) the contents of query_raw: which includes the url and query info. Response is written to curl_response.tmp
{
	replace_spaces(query_raw,query_stringified);
				fprintf(log_file,"\t\tquery string: %s and len: %d\n",query_raw, strlen(query_raw));
				fprintf(log_file,"\t\t       stringified: %s\n",query_stringified);
	fp = fopen("curl_response.tmp","wb");  //wb=write binary: overwrites and creates if neeed. 
	curl_easy_setopt(curl, CURLOPT_URL, query_stringified);   
	curl_easy_setopt(curl, CURLOPT_WRITEDATA,fp);   //if you DONT do this, it will send the output to stdio instead of this file. have to go to a file, and then read the file. Silly, why not go straight to a char array variable? because thats more complicated and needs callback etc.
    res = curl_easy_perform(curl);	
	fclose(fp);
}
//******************************************************************************
void process_1st_packet(void)  //parses response to first SQL query (callsign packet)
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to just open and read the file anyway? Fah-Q, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
				fprintf(log_file,"\t1st query NO RESPONSE\n");
	else
	{
	_1st_pak_found=1;
				fprintf(log_file,"1st query response was: %s",site_response);
	}
    char *token;
    int count = 0;
	token = strtok(site_response, "\t");  // Tokenize using tab separator
    while (token != NULL) 
	{
        count++;
        if (count == 5) snprintf(_4chargrid,5,"%s",token);      //5 instead of 4 cause snprintf automatically wants to make last one a NULL            
	    if (count == 3) snprintf(_uploader,sizeof(_uploader),"%s",token); 
		token = strtok(NULL, "\t");
	}
	fclose(fp);
				if (_1st_pak_found==1) fprintf(log_file, "1st packet PARSED: uploader: %s and regular callsign (already known i hope): %s and the grid: %s\n",_uploader,callsign,_4chargrid);
}
//******************************************************************************

void process_2nd_packet(void)  //parses response to second SQL query (basic telemetry packet)
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to open and read the file anyway? Fah-Q you, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
				fprintf(log_file,"\t2nd query  NO RESPONSE\n");
	else
	{
	_2nd_pak_found=1;
				fprintf(log_file,"2nd query response was: %s",site_response);
	}			
	char *token;
    int count = 0;
	token = strtok(site_response, "\t");  // sets token as a pointer to first instance of TAB
    while (token != NULL) 
	{
        count++;
        if (count == 3) snprintf(_telem_callsign,7,"%s",token);  
        if (count == 4) snprintf(_telem_grid,5,"%s",token);       
		if (count == 7) _telem_power =  atoi(token);        
		token = strtok(NULL, "\t");         // sets token as a pointer to the NEXT instance of TAB
	}
				if (_2nd_pak_found==1) fprintf(log_file,"2nd query RAW: Telem callsign:%s , grid:%s , power:%d\n",_telem_callsign,_telem_grid,_telem_power);
	fclose(fp);
}

//******************************************************************************

void process_3rd_packet(void)  //parses response to 3rd SQL query (Extended telemetry packet)
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to open and read the file anyway? Fah-Q you, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
				fprintf(log_file,"\t3rd query NO RESPONSE\n");
	else
	{
		_3rd_pak_found=1;
				fprintf(log_file,"3rd query response was:\n %s",site_response);
	}
	char *token;
    int count = 0;
	token = strtok(site_response, "\t");  // sets token as a pointer to first instance of TAB
    while (token != NULL) 
	{
        count++;
        if (count == 3) snprintf(_telem_callsign,7,"%s",token);  
         if (count == 4) snprintf(_telem_grid,5,"%s",token);       
		if (count == 7) _telem_power =  atoi(token);        
		token = strtok(NULL, "\t");         // sets token as a pointer to the NEXT instance of TAB
	}
				if (_3rd_pak_found==1) fprintf(log_file,"3rd query RAW: Telem callsign:%s , grid:%s , power:%d\n",_telem_callsign,_telem_grid,_telem_power);
	fclose(fp);
}



//******************************************************************************

void decode_telem_data(void)  //input: callsign (not including char 0 and 2), outputs: grid56 ,altitude, (1st of the 32 bit words)
{
uint32_t _32_bits;

int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
int v2= _telem_callsign[3]-'A'; 
int v3= _telem_callsign[4]-'A'; 
int v4= _telem_callsign[5]-'A'; 

_32_bits=v1;                                     //pack
_32_bits *= 26;_32_bits+=v2; 
_32_bits *= 26;_32_bits+=v3; 
_32_bits *= 26;_32_bits+=v4; 

altitude=20*(_32_bits%1068);  _32_bits=_32_bits/1068;    //unpack
grid6='A'+(_32_bits%24); _32_bits=_32_bits/24;
grid5='A'+(_32_bits%24);
snprintf(_6_char_grid, 7,"%s%c%c",_4chargrid,grid5,grid6);
						if (_2nd_pak_found==1) fprintf(log_file,"BASIC-TELEM DECODE: alt: %d grid56: %c%c ",altitude,grid5,grid6);

//input: 4 char grid and power, outputs: bits, speed, volts, temp, (2nd of the 32 bit words)
//_telem_grid ( 4 chars, 2 letts two nums) ,_telem_power  _telem_power_CONVERTED(int)
  int _telem_power_CONVERTED;
  const int8_t valid_dbm[19] = { 0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};
  for(int i = 0; i < 19; i++)
  {
    if(_telem_power == valid_dbm[i])
    {
      _telem_power_CONVERTED=i;
    }
  }
  
		         _32_bits=_telem_grid[0]-'A';   //pack
_32_bits *= 18;	_32_bits+=_telem_grid[1]-'A';	
_32_bits *= 10;	_32_bits+=_telem_grid[2]-'0';	
_32_bits *= 10;	_32_bits+=_telem_grid[3]-'0';	
_32_bits *= 19;	_32_bits+=_telem_power_CONVERTED;	

basic_telem_bit=_32_bits%2;_32_bits=_32_bits/2;//telem bit           //unpack
bit1=_32_bits%2;_32_bits=_32_bits/2;//gps valid_bit
_knots=_32_bits%42;_32_bits=_32_bits/42;
_volts=_32_bits%40;_32_bits=_32_bits/40;
_temp=(_32_bits%90)-50;

						if (_2nd_pak_found==1) fprintf(log_file,"\t basic_telem_bit:%d  bit1:%d  knots:%d volts:%d  temp:%d",basic_telem_bit,bit1,_knots,_volts,_temp);
						if (_2nd_pak_found==1) fprintf(log_file," raw telem power of %d normalized to %d\n",_telem_power,_telem_power_CONVERTED);
}

//******************************************************************************

void 	decode_extended_telem_data(void)  //input: callsign (not including char 0 and 2), grid and power , outputs: grid7,8,9,10 minutes ince boot and gps lock
{
uint64_t _64_bits;

  int _telem_power_CONVERTED;
  const int8_t valid_dbm[19] = { 0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};
  for(int i = 0; i < 19; i++)
  {
    if(_telem_power == valid_dbm[i])
      _telem_power_CONVERTED=i;
  }

int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
int v2= _telem_callsign[3]-'A'; 
int v3= _telem_callsign[4]-'A'; 
int v4= _telem_callsign[5]-'A'; 
	
			  ;_64_bits=v1;                         //pack
_64_bits *= 26;_64_bits+=v2; 
_64_bits *= 26;_64_bits+=v3; 
_64_bits *= 26;_64_bits+=v4; 
	
_64_bits *= 18; _64_bits+=_telem_grid[0]-'A';   		//btw, first two chars have range of 18, but chars 5,6 have range of 24. wikiepdia suggest  9,10 probably also base 24 "The fifth and subsequent pairs are not formally defined, but recursing to the third and fourth pair algorithms is a possibility"
_64_bits *= 18;	_64_bits+=_telem_grid[1]-'A';	
_64_bits *= 10;	_64_bits+=_telem_grid[2]-'0';	
_64_bits *= 10;	_64_bits+=_telem_grid[3]-'0';	
_64_bits *= 19;	_64_bits+=_telem_power_CONVERTED;	

							//unpack

					_64_bits=_64_bits/2;      //telem type
					_64_bits=_64_bits/4;	  //reserved
					_64_bits=_64_bits/16;     //Type
					_64_bits=_64_bits/5;	  //slot	
grid7=_64_bits%10;  _64_bits=_64_bits/10;           
grid8=_64_bits%10;  _64_bits=_64_bits/10;    
grid9=_64_bits%24;  _64_bits=_64_bits/24;    
grid10=_64_bits%24;  _64_bits=_64_bits/24;    
mins_since_boot=(_64_bits%1001);  _64_bits=_64_bits/1001;    
mins_since_lock=(_64_bits%1001);  _64_bits=_64_bits/1001;    

						if (_3rd_pak_found==1) fprintf(log_file,"3rd packet DEXT DECODE: grid7: %d grid8: %d grid9: %d grid10: %d since_boot %d  since_gps %d  \n",grid7,grid8,grid9,grid10,mins_since_boot,mins_since_lock);

}

//***************************************************************************
void send_to_sondehub(void)  //via json payload
{
	    char response_buffer[MAX_RESPONSE_SIZE];

    struct MemoryBuffer mem = {
        .data = response_buffer,
        .size = 0,
        .max_size = sizeof(response_buffer)
    };
	
	
	
	const char *url = "http://api.v2.sondehub.org/amateur/telemetry";
	char datetime[30];
    get_iso_utc_time(datetime, sizeof(datetime));
	char json_payload[701];

	snprintf(json_payload, 700,"[{"
    "\"software_name\":\"TWITS github.com/EngineerGuy314/TWITS\","
    "\"software_version\":\"2.2 June19_2025\","
	"\"modulation\":\"WSPR\","
	"\"datetime\":\"%s\","
	"\"comment\":\"%s\","
	"\"detail\":\"%s%s\","
    "\"uploader_callsign\":\"%s\","
    "\"payload_callsign\":\"%s%s\","
    "\"lat\":%f,"
    "\"lon\":%f,"
    "\"alt\":%d,"
	"\"sats\":%d,"
	"\"temp\":%d,"
	"\"batt\":%f"
    "}]",datetime,comment,detail_prepend_msg,detail,_uploader,callsign,payload_suffix,lat,lon,altitude,_knots,_temp,2+(((_volts*5)+200)/(float)100));           

												fprintf(log_file,"JASON PAYLOAD to SONDEHUB is: %s\n",json_payload);

		struct curl_slist *headers = NULL;	      
        headers = curl_slist_append(headers, "Accept: text/plain" );
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "User-Agent: axios/1.7.9");
        headers = curl_slist_append(headers, "Accept-Encoding: gzip, compress, deflate, br" );
        headers = curl_slist_append(headers, "Connection: keep-alive");
        headers = curl_slist_append(headers, "Host: api.v2.sondehub.org");
	
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
   	curl_easy_setopt(curl, CURLOPT_URL, url);	
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); 
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	
	 curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&mem);
	
    res = curl_easy_perform(curl);

										fprintf(log_file,"\t results from curl put was %s\n",curl_easy_strerror(res));
										fprintf(log_file,"Repo Buffer  from curl put was %s\n",response_buffer);
}
//***************************************************************************
int main(int argc, char *argv[]) {
	

	init(argc,argv[2]); //some boring stuff
	epoch_time = time(NULL); 
	curl = curl_easy_init();
											//printf("arg count: %d, callsio: %s arg 2 channel: %s arg3 comment: %s arg4 detail: %s Extened_telem_type %s\n",argc,argv[1],argv[2],argv[3],argv[4],argv[5]);
											if (argc<5) fprintf(log_file,"!! got %d arguments, need 5 or 6!!\n",argc);
	if (argc>=6) 
		_extended_telem=atoi(argv[5]);
	else
		_extended_telem=0;
	int chan_num=atoi(argv[2]);   //convert chan # to minute, id13 and lane
	_id1[0]='1';
	if  (chan_num<200) _id1[0]='0';
	if  (chan_num>399) _id1[0]='Q';
	int id3 = (chan_num % 200) / 20;
	_id3[0]=id3+'0';		
	_freq_lane = 1+((chan_num % 20)/5);   //1-4
	_start_minute[0] = '0' + (2*(((chan_num % 5)+14)%5));
	_start_minute[1]=0;_id1[1]=0;_id3[1]=0;  //null terminations

	 switch(_freq_lane)   //set limits for bin-ing of telemetry
		{
		case 1: low_freq_limit=14097000; high_freq_limit=14097040; break;    
		case 2: low_freq_limit=14097040; high_freq_limit=14097080; break; 
		case 3: low_freq_limit=14097120; high_freq_limit=14097160; break; 
		case 4: low_freq_limit=14097160; high_freq_limit=14097200; break; 
		}

											fprintf(log_file, "\targ count: %d, callsio: %s start minute: %s  id1: %s id3: %s SpinLock: %d channel as integer: %d freq lane: %d low/high freq limits %d %d\n",argc,argv[1],_start_minute,_id1,_id3,was_spinlocked,chan_num,_freq_lane,low_freq_limit,high_freq_limit); 
	snprintf(callsign, 7, "%s",argv[1]);
	snprintf(payload_suffix, 5, "-%s",argv[2]);  //normally suffix is chann #
	snprintf(comment,99,"%s",argv[3]);
	snprintf(detail,99,"%s",argv[4]); 

    // Build query string for callsign packet from wspr.live
	start_minute_of_packet = atoi(_start_minute);
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, rx_sign, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25')  AND (tx_sign LIKE '%s') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,argv[1],low_freq_limit,high_freq_limit);
	send_SQL_query();
	process_1st_packet();    //extracts _uploader and _4chargrid
	
    // Build query string for telemetry packet from wspr.live
	start_minute_of_packet = (atoi(_start_minute)+2)%10;
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25') AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
	send_SQL_query();	
	process_2nd_packet();    //extracts _telem_callsign _telem_grid and _telem_power
		if (_2nd_pak_found==0) //if no match, try again without frequency binning
		{
							fprintf(log_file,"\t No 2nd match, trying again without Frequency bin\r");
			snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3);
			send_SQL_query();	
			process_2nd_packet();    //extracts _telem_callsign _telem_grid and _telem_power
			if (_2nd_pak_found==1) strcpy(detail_prepend_msg, "NO FREQ BIN MATCH! ");
		}
	decode_telem_data();    //unpacks the encoded info from telemetry packet (into grid chars 5,6 and altitude) and converts grid to lat/lon. also gets _knots,_volts,_temp

   // Build query string for EXTENDED telemetry packet from wspr.live
	start_minute_of_packet = (atoi(_start_minute)+4)%10;
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25')  AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
	send_SQL_query();	
	process_3rd_packet();       	 //extracts _telem_callsign _telem_grid and _telem_power
	decode_extended_telem_data();    // so far only does that one scenario

		if 	((_1st_pak_found==1)&&(_2nd_pak_found==1)&&(basic_telem_bit==1))   //only if you received two packets AND the 2nd packet was basic telem (not someone else's Extended Telem)
		{
								fprintf(log_file,"SENDING TO SONDEHUB (regular)!! two paks were found and basic_telem was on \n");
			maidenhead_to_latlon(_6_char_grid, &lat, &lon);	
			send_to_sondehub();     // send to Sondehub via json payload
		}

		if 	((_1st_pak_found==1)&&(_2nd_pak_found==1)&&(_3rd_pak_found==1)&&(basic_telem_bit==1)&&(_extended_telem==1))   // if you received 3 packets AND the 2nd packet was basic telem AND you have selected extended telem decode
		{
								fprintf(log_file,"SENDING -EXTENDED- TO SONDEHUB !! Three paks were found and basic_telem was on \n");
			snprintf(payload_suffix, 6, "-%se",argv[2]);   //appends an 'e' at end of suffix
			snprintf(detail,100,"ET high-res. mins since boot: %d mins since GPS lock:%d",mins_since_boot,mins_since_lock);        
			ten_char_maidenhead_to_latlon(_6_char_grid, grid7, grid8, grid9, grid10, &lat, &lon);
			send_to_sondehub();     
		}

	curl_easy_cleanup(curl); curl_global_cleanup();
								fprintf(log_file,"\n\n");
	fclose(log_file); close(fd);  //close log and lockfile

    return 0;
}

