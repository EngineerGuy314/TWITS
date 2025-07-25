// to compile:    gcc twits.c -o twits.exe -lcurl
// args: callsign, U4B_type_channel, comment, [ExtendedPrecisionTelemetry-enable]

// for debug compile:   gcc -fsanitize=address -g twits.c -o twits.exe -lcurl
//for compile with extra warnings: gcc -Wall -Wextra -Wformat twits.c -o twits.exe -lcurl

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

char LOGFILE_PATH[256];

time_t epoch_time;
char query_raw[501];           //will cause segflt if too small (should prolly make dynamic)
char query_stringified[501];
char ET_results3[201];
char ET_results4[201];
char ET_results5[201];
char ET_res_buf[201];
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
char detail[601];
char comment[200];
int second_pack_was_Basic_Telem;
int bit1;
int _knots;
int _volts;
int _temp;
int was_spinlocked=0;
int fd;
char _start_minute[2];
int chan_num;
char _id1[2];
char _id3[2];
int _freq_lane;
int _HIGH_RES_Positioning_from_ET_enabled;   //special case of type 5 DEXT from picoWSRPRer
int start_minute_of_packet;
int _1st_pak_found;
int _2nd_pak_found;
int _TELEM_pak_found;
int _freq;
int low_freq_limit;
int high_freq_limit;
int Generic_ET_is_Enabled=0;
struct ET_data {          //for generic (custom message) Extended Telemetry
    char name[40];
    char units[10];
	int low;
    int high;
    int step;
    int slot;  //3 4 or 5
    };
struct ET_data ET_config_array[31];
	char ET_CONFIG_FILE_PATH[256];
	char line[500];
int High_res_TELEM_pak_found;
const int8_t valid_dbm[19] = { 0, 3, 7, 10, 13, 17, 20, 23, 27, 30, 33, 37, 40, 43, 47, 50, 53, 57, 60};


#define SECONDS_TO_LOOK_BACK 600
////********************************
void get_iso_utc_time(char *buffer, size_t size) {
    struct timeval tv;struct tm *tm_info;gettimeofday(&tv, NULL);tm_info = gmtime(&tv.tv_sec);strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", tm_info);snprintf(buffer + 19, size - 19, ".%03ldZ", tv.tv_usec / 1000); // Add milliseconds
}
void get_iso_utc_timeSIMPLE(char *buffer, size_t size) {
    struct timeval tv;struct tm *tm_info;gettimeofday(&tv, NULL);tm_info = gmtime(&tv.tv_sec);strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}
//***************************************************************************
void init(const char *arg) {
	
	//**************** spin lock stuff ********************************************************************
	const char *lockfile = "/tmp/mylockfile.lock";              //spin lock if called more than once
    fd = open(lockfile, O_CREAT | O_RDWR, 0666);
    // Spinlock: keep trying to get the lock
    while (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        // Lock is held by another process; sleep briefly to avoid busy wait
		was_spinlocked=1;
        usleep(100000); // 100 ms
    }
	//********************* open log file***********************************************************
	struct stat st;
	snprintf(LOGFILE_PATH, sizeof(LOGFILE_PATH), "twits_log_ch_%s.txt", arg);  //create a unique logfile with channel name (if multiple instances)
                                            //tidy up logfile if its too big
    // Check if logfile exists and its size
	if (stat(LOGFILE_PATH, &st) == 0) {
		if (st.st_size >= 4000000) {  //max 4MB logfile
			if (remove(LOGFILE_PATH) != 0) {
				printf("LogFile delete error\n");}}}	
	log_file = fopen(LOGFILE_PATH,"a");  	
												char datetime[30];get_iso_utc_timeSIMPLE(datetime, sizeof(datetime));fprintf(log_file,"%s\n",datetime);	
	//******** decipher chanel number *********************************************************************************************
	chan_num=atoi(arg);	   	//get channel number         (arg is actually argv[2] passed in from main)  	
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
														fprintf(log_file, "\tstart minute: %s  id1: %s id3: %s SpinLock: %d channel as integer: %d freq lane: %d low/high freq limits %d %d\n",_start_minute,_id1,_id3,was_spinlocked,chan_num,_freq_lane,low_freq_limit,high_freq_limit); 
	//************** Check for ET (extended telemetry) config file ********************************************************************************
	
	snprintf(ET_CONFIG_FILE_PATH, sizeof(ET_CONFIG_FILE_PATH), "twits_ET_CONFIG_ch_%s.txt", arg);
	detail_prepend_msg[0]=0;
	//check for Extended Telemetry (ET) config file	  
		int count=0;
		FILE *ET_config_file = fopen(ET_CONFIG_FILE_PATH, "r");
			if (ET_config_file) {    //if succefully found and opened
																					fprintf(log_file,"Found ET config file %s\n",ET_CONFIG_FILE_PATH);	
				while (fgets(line, sizeof(line), ET_config_file)) {
						// Skip comments or blank lines
						if (line[0] == '#' || line[0] == '\n' || line[0] == '\r'  || line[0] == ' ')
							continue;
						if (strncmp(line, "<EOF>", 5) == 0) break;  //quit if <EOF> keyword found. needed because may use space below that for storage between runs

						// Parse line
						char *name = strtok(line, ",");
						char *units_str = strtok(NULL, ",");
						char *low_str = strtok(NULL, ",");
						char *high_str = strtok(NULL, ",");
						char *step_str = strtok(NULL, ",");
						char *slot_str = strtok(NULL, ",\n");

						if (!name || !units_str || !low_str || !high_str || !step_str || !slot_str) {
							fprintf(log_file,"BAD line in ET config file,valid counts: %d first char (ascii):%d full line:%s (expecting name,unit,low,high,step,slot)\n",count,line[0],line);	
							continue;
						}

						// Store into array
						strncpy(ET_config_array[count].name, name, sizeof(ET_config_array[count].name) - 1);
						ET_config_array[count].name[sizeof(ET_config_array[count].name) - 1] = '\0'; //null terminate, just in case
						strncpy(ET_config_array[count].units, units_str, sizeof(ET_config_array[count].units) - 1);
						ET_config_array[count].units[sizeof(ET_config_array[count].units) - 1] = '\0';  //null terminate, just in case
						ET_config_array[count].low = atoi(low_str);
						ET_config_array[count].high = atoi(high_str);
						ET_config_array[count].step = atoi(step_str);
						ET_config_array[count].slot = atoi(slot_str);   
						count++; 
						Generic_ET_is_Enabled=1; //if we got this far, assume at least some valid lines were found
						ET_results3[0]=0;ET_results4[0]=0;ET_results5[0]=0; //just in case null termiantes the string used to store ET results
													if (count>30) {fprintf(log_file,"EXCEEDED 30 pieces of ET data!\n"); fclose(log_file);printf("EXCEEDED 30 pieces of ET data!");exit(1);}
					}
				fclose(ET_config_file);
			}
			  //dump array contents for testing
			  //for(int i = 0; i < 30; i++)  printf("array elment %d name %s units %s low %d high %d step %d slot %d \n",i,ET_config_array[i].name,ET_config_array[i].units,ET_config_array[i].low,ET_config_array[i].high,ET_config_array[i].step,ET_config_array[i].slot);
			  //for(int i = 0; i < 30; i++)  fprintf(log_file,"array elment %d name %s units %s low %d high %d step %d slot %d \n",i,ET_config_array[i].name,ET_config_array[i].units,ET_config_array[i].low,ET_config_array[i].high,ET_config_array[i].step,ET_config_array[i].slot);
		//********************* misc ************************
		epoch_time = time(NULL);
		detail[0]=0;
}
//************************************************************
void remove_temporary_ET_data(void)   //after a succesful send to sondehub (when callsign and basic telem were found) clear out any remaining temporary ET slot results
{
    char buffer[1024];
    long offset = 0;
	long result;
	
FILE *fp = fopen(ET_CONFIG_FILE_PATH, "r+"); // read + write
    if (fp)
	{
		while (fgets(buffer, sizeof(buffer), fp)) 
		{
			char *pos = strstr(buffer, "<EOF>");
			if (pos) {result = offset + (pos - buffer);break;}
			offset += strlen(buffer);
		}
		fseek(fp, result+5, SEEK_SET);
		ftruncate(fileno(fp), result+5);		
		fseek(fp, 0, SEEK_END);
		fputs("<SL3><SL4><SL5><END>", fp);
		fflush(fp);
		fclose(fp);
	}
}
//************************************************************
void load_temporary_ET_data(void)   //on startup loads any ET slot data that was collected but not sent (because callsign and basic telem weren't ready)
{
	char *start;
	FILE *fp = fopen(ET_CONFIG_FILE_PATH, "r"); // read 
    if (fp)
		{
			fseek(fp, 0, SEEK_END);
			long filesize = ftell(fp);
			rewind(fp);
			char *buffer = malloc(filesize + 1);
			fread(buffer, 1, filesize, fp);
			buffer[filesize] = '\0';  // Null-terminate
			fclose(fp);
			start = 5 + strstr(buffer, "<SL3>");
			snprintf(ET_results3,strstr(buffer, "<SL4>")-strstr(buffer, "<SL3>")-4,"%s",start);
			start = 5 + strstr(buffer, "<SL4>");
			snprintf(ET_results4,strstr(buffer, "<SL5>")-strstr(buffer, "<SL4>")-4,"%s",start);
			start = 5 + strstr(buffer, "<SL5>");
			snprintf(ET_results5,strstr(buffer, "<END>")-strstr(buffer, "<SL5>")-4,"%s",start);
			free(buffer);
			//printf("the ET results:\n3-%s\n4-%s\n5-%s\n", ET_results3,ET_results4,ET_results5);
			//fprintf(log_file,"!!! LOADING FROM FILE: \n\t3-%s\n\t4-%s\n\t5-%s\n", ET_results3,ET_results4,ET_results5);
		}
}

//***********************************************************

void save_temporary_ET_data(void)    //any ET slot data that was collected but not sent (because callsign and basic telem weren't ready) saved to the config file for next time
{
    char buffer[1024];
    long offset = 0;
	long result;
	
FILE *fp = fopen(ET_CONFIG_FILE_PATH, "r+"); // read + write
    if (fp)
	{
		while (fgets(buffer, sizeof(buffer), fp)) 
		{
			char *pos = strstr(buffer, "<EOF>");
			if (pos) {result = offset + (pos - buffer);break;}
			offset += strlen(buffer);
		}
		fseek(fp, result+5, SEEK_SET);
		ftruncate(fileno(fp), result+5);		
		fseek(fp, 0, SEEK_END);
		fputs("<SL3>", fp); fputs(ET_results3, fp);
		fputs("<SL4>", fp); fputs(ET_results4, fp);
		fputs("<SL5>", fp); fputs(ET_results5, fp);
		fputs("<END>", fp);
		fflush(fp);
		fclose(fp);
	}
		//fprintf(log_file,"!!! SAVING TO FILE: \n\t3-%s\n\t4-%s\n\t5-%s\n", ET_results3,ET_results4,ET_results5);
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

void process_possible_TELEM_packet(int _slot)  //parses response to SQL query for potential Extended telemetry packet. puts values into _telem_callsign,_telem_grid,_telem_power
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to open and read the file anyway? Fah-Q you, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
										fprintf(log_file,"\tNO query  RESPONSE for slot %d\n",_slot);
	else
	{
		_TELEM_pak_found=1;
										fprintf(log_file,"query response for slot %d was:\n %s",_slot,site_response);
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
												if (_TELEM_pak_found==1) fprintf(log_file," query for slot %d  RAW: Telem callsign:%s , grid:%s , power:%d\n",_slot,_telem_callsign,_telem_grid,_telem_power);
	fclose(fp);
}

//******************************************************************************

void decode_BASIC_telem_data(void)  //input: callsign (not including char 0 and 2), outputs: grid56 ,altitude, (1st of the 32 bit words)
{
uint32_t _32_bits;

int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
int v2= _telem_callsign[3]-'A'; 
int v3= _telem_callsign[4]-'A'; 
int v4= _telem_callsign[5]-'A'; 

			   _32_bits =v1;                                     //pack
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

  for(int i = 0; i < 19; i++) 
    if(_telem_power == valid_dbm[i]) _telem_power_CONVERTED=i;
  
		         _32_bits=_telem_grid[0]-'A';   //pack
_32_bits *= 18;	_32_bits+=_telem_grid[1]-'A';	
_32_bits *= 10;	_32_bits+=_telem_grid[2]-'0';	
_32_bits *= 10;	_32_bits+=_telem_grid[3]-'0';	
_32_bits *= 19;	_32_bits+=_telem_power_CONVERTED;	

													 //unpack
second_pack_was_Basic_Telem=_32_bits%2;_32_bits=_32_bits/2;      //telem bit
bit1=_32_bits%2;_32_bits=_32_bits/2;               	 //gps valid_bit
_knots=_32_bits%42;_32_bits=_32_bits/42;
_volts=_32_bits%40;_32_bits=_32_bits/40;
_temp=(_32_bits%90)-50;

									if (_2nd_pak_found==1) fprintf(log_file,"\t second_pack_was_Basic_Telem:%d  bit1:%d  knots:%d volts:%d  temp:%d",second_pack_was_Basic_Telem,bit1,_knots,_volts,_temp);
									if (_2nd_pak_found==1) fprintf(log_file," raw telem power of %d normalized to %d\n",_telem_power,_telem_power_CONVERTED);
}

//******************************************************************************

void 	decode_HIGH_RES_POSITION_from_ET(void)  //input: callsign (not including char 0 and 2), grid and power , outputs: grid7,8,9,10 minutes ince boot and gps lock
{
uint64_t _64_bits;

  int _telem_power_CONVERTED;
  for(int i = 0; i < 19; i++)
  {
    if(_telem_power == valid_dbm[i])
      _telem_power_CONVERTED=i;
  }

int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
int v2= _telem_callsign[3]-'A'; 
int v3= _telem_callsign[4]-'A'; 
int v4= _telem_callsign[5]-'A'; 
	
			  ;_64_bits =v1;                         //pack
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
grid9=_64_bits%24;  _64_bits=_64_bits/24;                      //btw, first two chars have range of 18, but chars 5,6 have range of 24. wikiepdia suggest  9,10 probably also base 24 "The fifth and subsequent pairs are not formally defined, but recursing to the third and fourth pair algorithms is a possibility"
grid10=_64_bits%24;  _64_bits=_64_bits/24;    
mins_since_boot=10*(_64_bits%101);  _64_bits=_64_bits/101;    
mins_since_lock=10*(_64_bits%101);  _64_bits=_64_bits/101;  


						if (_TELEM_pak_found==1) fprintf(log_file,"3rd packet DEXT DECODE: grid7: %d grid8: %d grid9: %d grid10: %d since_boot %d  since_gps %d  \n",grid7,grid8,grid9,grid10,mins_since_boot,mins_since_lock);

High_res_TELEM_pak_found=1;
}
//****************************************************************************
size_t discard_response(char *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

//***************************************************************************
void send_to_sondehub(void)  //via json payload
{
	const char *url = "http://api.v2.sondehub.org/amateur/telemetry";
	char datetime[30];
    get_iso_utc_time(datetime, sizeof(datetime));
	char json_payload[701];
	if (_knots==0) _knots=1; //i think sondehub ignores spots with 0 sattellites, this forces to at least 1
	snprintf(json_payload, 700,"[{"
	"\"software_name\":\"TWITS github.com/EngineerGuy314/TWITS\","
	"\"software_version\":\"3.3 July25_2025\","
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
	headers = curl_slist_append(headers, "Accept-Encoding: gzip, compress, deflate, br" );
	headers = curl_slist_append(headers, "Connection: keep-alive");
	headers = curl_slist_append(headers, "Host: api.v2.sondehub.org");	
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
   	curl_easy_setopt(curl, CURLOPT_URL, url);	
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT"); 
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
	res = curl_easy_perform(curl);

														fprintf(log_file,"\t results from curl put was %s\n",curl_easy_strerror(res));
}

//***************************************************************************

void decode_generic_ET_for_slot(int _slot)    //looks at (_telem_callsign+_telem_grid +_telem_power) and appends result to ET_results if valid
	{
		uint64_t _64_bits;
		int _telem_type;
		int slot_val;
		int range;
		int data;
		int _telem_power_CONVERTED;

													fprintf(log_file,"\tdecoding generic ET packet in slot # %d \n",_slot);

		for(int i = 0; i < 19; i++)
			{
				if(_telem_power == valid_dbm[i])
				_telem_power_CONVERTED=i;
			}

		int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
		int v2= _telem_callsign[3]-'A'; 
		int v3= _telem_callsign[4]-'A'; 
		int v4= _telem_callsign[5]-'A'; 
			
					  ;_64_bits =v1;                         // PACK   (shift-left, pack, repeat...)
		_64_bits *= 26;_64_bits+=v2; 
		_64_bits *= 26;_64_bits+=v3; 
		_64_bits *= 26;_64_bits+=v4; 
		_64_bits *= 18; _64_bits+=_telem_grid[0]-'A';   		//btw, first two chars have range of 18, but chars 5,6 have range of 24. wikiepdia suggest  9,10 probably also base 24 "The fifth and subsequent pairs are not formally defined, but recursing to the third and fourth pair algorithms is a possibility"
		_64_bits *= 18;	_64_bits+=_telem_grid[1]-'A';	
		_64_bits *= 10;	_64_bits+=_telem_grid[2]-'0';	
		_64_bits *= 10;	_64_bits+=_telem_grid[3]-'0';	
		_64_bits *= 19;	_64_bits+=_telem_power_CONVERTED;	

														   // UN-PACK  (extract via modulo, shift-right, repeat...)
		_telem_type=_64_bits%2;	_64_bits=_64_bits/2;      //telem type  (0 for Extended, 1 for Basic)
		/* xxx =_64_bits%4;*/	_64_bits=_64_bits/4;	  //reserved
		/* xxx =_64_bits%16;*/	_64_bits=_64_bits/16;     //Type (reserved)
		slot_val=_64_bits%5;	_64_bits=_64_bits/5;	  //slot	


		ET_res_buf[0]=0;
		slot_val++; //needed because spec numbers slots 0-4 not 1-5

		if ((_telem_type==0)&&(slot_val==_slot))
			{
				//cycle through ET_config_array, for any matching slots extract data and append to ET_results         
														fprintf(log_file,"about to cycle through ET Config array\n");
				for(int i = 0; i < 31; i++)
				{
					if (ET_config_array[i].slot==_slot)
					{
						range=1+ET_config_array[i].high-ET_config_array[i].low;
						data=_64_bits%range;_64_bits=_64_bits/range;      						
						snprintf(ET_res_buf + strlen(ET_res_buf), sizeof(ET_res_buf) - strlen(ET_res_buf), "%s: %d (slot:%d) ",ET_config_array[i].name,data,_slot);			
					}
				}
								
				if(_slot==3) snprintf(ET_results3,strlen(ET_res_buf),"%s",ET_res_buf);
				if(_slot==4) snprintf(ET_results4,strlen(ET_res_buf),"%s",ET_res_buf);
				if(_slot==5) snprintf(ET_results5,strlen(ET_res_buf),"%s",ET_res_buf);
			}

	
	}
//***************************************************************************
int main(int argc, char *argv[]) {
	if (argc<4) {printf("NOT ENOUGH ARGUMENTS !!! need 4 or 5 (including prog name). example: twits.exe callsign, channel, comment, [high-res telem]. arg count was : %d, \n",argc);fprintf(log_file,"Not enough cmd line args!\n"); fclose(log_file);exit(1);}
	init(argv[2]); //some boring stuff	 
	load_temporary_ET_data();
	curl = curl_easy_init();
														
	if (argc>=5) 
		if( atoi(argv[4])==1) _HIGH_RES_Positioning_from_ET_enabled=1;
	else
		_HIGH_RES_Positioning_from_ET_enabled=0;
	
														if (_HIGH_RES_Positioning_from_ET_enabled && Generic_ET_is_Enabled)  fprintf(log_file,"!!!!!!!!! Found Generic ET config file AND found argument to enable High Res positioning!! not sure if that will work, untested!!!\n");

	snprintf(callsign, 7, "%s",argv[1]);
	snprintf(payload_suffix, 5, "-%s",argv[2]);  //normally suffix is chan#
	snprintf(comment,99,"%s",argv[3]);
	

// Build query string for callsign packet from wspr.live
	start_minute_of_packet = atoi(_start_minute);
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, rx_sign, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25')  AND (tx_sign LIKE '%s') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,argv[1]);

	send_SQL_query();
	process_1st_packet();    //extracts _uploader and _4chargrid   
	
// Build query string for basic telemetry packet from wspr.live
	start_minute_of_packet = (atoi(_start_minute)+2)%10;
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25') AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
	send_SQL_query();	
	process_2nd_packet();    //extracts _telem_callsign _telem_grid and _telem_power
				/*if (_2nd_pak_found==0) //if no match, try again without frequency binning (DISABLED FOR NOW, prolly not needed. causes trouble more often than it helps?)
				{
													fprintf(log_file,"\t No 2nd match, trying again without Frequency bin\r");
					snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3);
					send_SQL_query();	
					process_2nd_packet();    //extracts _telem_callsign _telem_grid and _telem_power
					if (_2nd_pak_found==1) strcpy(detail_prepend_msg, "NO FREQ BIN MATCH! ");
				}*/
	decode_BASIC_telem_data();    //unpacks the encoded info from telemetry packet (into grid chars 5,6 and altitude) and converts grid to lat/lon. also gets _knots,_volts,_temp

	if (Generic_ET_is_Enabled || _HIGH_RES_Positioning_from_ET_enabled)
		{
		// Build query string for slot 3 EXTENDED telemetry packet from wspr.live
			start_minute_of_packet = (atoi(_start_minute)+4)%10;
			snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25')  AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
			send_SQL_query();   _TELEM_pak_found=0;
			process_possible_TELEM_packet(3);       	 //extracts _telem_callsign _telem_grid and _telem_power, sets _TELEM_pak_found if something found
			if (_TELEM_pak_found && _HIGH_RES_Positioning_from_ET_enabled)  decode_HIGH_RES_POSITION_from_ET();   	 // this is just for the niche high-precision positioning feature
			if (_TELEM_pak_found && Generic_ET_is_Enabled) 					decode_generic_ET_for_slot(3);    		 //looks at (_telem_callsign+_telem_grid +_telem_power) and appends result to ET_results if valid
		}
	if (Generic_ET_is_Enabled)
		{
		// Build query string for slot 4 EXTENDED telemetry packet from wspr.live
			start_minute_of_packet = (atoi(_start_minute)+6)%10;
			snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25')  AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
			send_SQL_query();   _TELEM_pak_found=0;
			process_possible_TELEM_packet(4);       	 //extracts _telem_callsign _telem_grid and _telem_power, sets _TELEM_pak_found if something found
			if (_TELEM_pak_found) decode_generic_ET_for_slot(4);     //looks at (_telem_callsign+_telem_grid +_telem_power) and appends result to ET_results if valid

		// Build query string for slot 5 EXTENDED telemetry packet from wspr.live
			start_minute_of_packet = (atoi(_start_minute)+8)%10;
			snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25')  AND (frequency>%d) AND (frequency<%d) ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_packet,_id1,_id3,low_freq_limit,high_freq_limit);
			send_SQL_query();   _TELEM_pak_found=0;
			process_possible_TELEM_packet(5);       	 //extracts _telem_callsign _telem_grid and _telem_power, sets _TELEM_pak_found if something found
			if (_TELEM_pak_found) decode_generic_ET_for_slot(5);     //looks at (_telem_callsign+_telem_grid +_telem_power) and appends result to ET_results if valid
		}
// if callsign and basic telem were found, send regular packet to sondehub
	if 	((_1st_pak_found==1)&&(_2nd_pak_found==1)&&(second_pack_was_Basic_Telem==1))   //only if you received two packets AND the 2nd packet was basic telem (not someone else's Extended Telem)
		{
													fprintf(log_file,"SENDING TO SONDEHUB (regular)!! two paks were found and basic_telem was on \n");
			maidenhead_to_latlon(_6_char_grid, &lat, &lon);	
			if (Generic_ET_is_Enabled) snprintf(detail,sizeof(detail),"%s %s %s",ET_results3,ET_results4,ET_results5);  //copies the ET resultss into detail field
			remove_temporary_ET_data();	
			send_to_sondehub();     // send to Sondehub via json payload
		}
	else  save_temporary_ET_data();
	
					//special niche feature: (NOT generic/custom ET)
					// if high-res position EXTENDED telem found, send special packet to sondehub
					if 	((_1st_pak_found==1)&&(_2nd_pak_found==1)&&(High_res_TELEM_pak_found==1)&&(second_pack_was_Basic_Telem==1)&&(_HIGH_RES_Positioning_from_ET_enabled==1))   // if you received 3 packets AND the 2nd packet was basic telem AND you have selected extended telem decode
						{
																			fprintf(log_file,"SENDING -EXTENDED- HIGH RES POSITION TO SONDEHUB !! Three paks were found and basic_telem of 2nd pak was on \n");
							snprintf(payload_suffix, 6, "-%se",argv[2]);   //appends an 'e' at end of suffix
							snprintf(detail,100,"ET high-res. mins since boot: %d mins since GPS lock:%d",mins_since_boot,mins_since_lock);        
							ten_char_maidenhead_to_latlon(_6_char_grid, grid7, grid8, grid9, grid10, &lat, &lon);
							send_to_sondehub();     
						}

	
	curl_easy_cleanup(curl); curl_global_cleanup(); fprintf(log_file,"\n\n");								
	fclose(log_file); close(fd);  //close log and lockfile	
	return 0;
}

