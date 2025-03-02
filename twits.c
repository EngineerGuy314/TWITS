//to compile:    gcc twits.c -o twits.exe -lcurl
//args: callsign, starting minute, id1, id3, [comment],[details]

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>
#include <unistd.h> // For sleep()
#include <sys/time.h>  

time_t epoch_time;
char query_raw[350];
char query_stringified[350];
FILE *fp;
FILE *log_file;

CURL *curl;
CURLcode res;
char site_response[2000];
char _4chargrid[5];
char _telem_callsign[7];
int _telem_power;
char _telem_grid[5];
int altitude;
int packet_count=0;
char grid5;
char grid6;
char grid[7]; 
double lat, lon;
char callsign[7];
char _uploader[7];
char detail[100];
char comment[100];

#define SECONDS_TO_LOOK_BACK 1200

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

void send_query_and_write_reponse_to_file(void)  
{
	replace_spaces(query_raw,query_stringified);

	fprintf(log_file,"Query string: %s and len: %ld\n",query_raw, strlen(query_raw));
//	printf("After stringification: %s\nits length: %li\n",query_stringified,strlen(query_stringified));

	fp = fopen("curl_response.tmp","wb");  //wb=write binary: overwrites and creates if neeed. 
	curl_easy_setopt(curl, CURLOPT_URL, query_stringified);   
	curl_easy_setopt(curl, CURLOPT_WRITEDATA,fp);   //if you DONT do this, it will send the output to stdio instead of this file
    res = curl_easy_perform(curl);
	
	fclose(fp);

}
//******************************************************************************
void process_1st_packet(void)
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to just open and read the file anyway? Fah-Q, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
		fprintf(log_file,"Response to 1st query was empty\n");
	else
	packet_count+=1;
	
	fprintf(log_file,"response to first packet was: %s",site_response);

    char *token;
    int count = 0;
	token = strtok(site_response, "\t");  // Tokenize using tab separator
    while (token != NULL) 
	{
        count++;
        if (count == 11) snprintf(_4chargrid,5,"%s",token);      //5 instead of 4 cause snprintf automatically wants to make last one a NULL            
	    if (count == 4) snprintf(_uploader,7,"%s",token); 

		token = strtok(NULL, "\t");
	}
	fclose(fp);

fprintf(log_file, "Parsing of 1st packet reveals: uploader: %s and regular callsign (already known i hope): %s and the grid: %s\n",_uploader,callsign,_4chargrid);
}
//******************************************************************************

void process_2nd_packet(void)
{
	fp = fopen("curl_response.tmp","r");  //why make curl put results into a file, if your going to open and read the file anyway? Fuck you, thats why.
	if ((  fgets(site_response, sizeof(site_response), fp)==NULL))
		fprintf(log_file,"Response to 2nd query was empty\n");
	else
	packet_count+=1;



	fprintf(log_file,"REPONSE TO SECOND:\n %s",site_response);

	char *token;
    int count = 0;
	token = strtok(site_response, "\t");  // Tokenize using tab separator
    while (token != NULL) 
	{
        count++;
        if (count == 3) snprintf(_telem_callsign,7,"%s",token);  
        if (count == 4) snprintf(_telem_grid,5,"%s",token);       
		if (count == 7) _telem_power =  atoi(token);        
		token = strtok(NULL, "\t");
	}

		printf("Telem callsign, grid, power: %s %s %d\n",_telem_callsign,_telem_grid,_telem_power);
		fprintf(log_file,"Telem callsign, grid, power: %s %s %d\n",_telem_callsign,_telem_grid,_telem_power);
	fclose(fp);
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
//******************************************************************************

void 	decode_telem_data(void)  //input: callsign (not including char 0 and 2), outputs: grid56 ,altitude, (only doing one of the 32 bit words)
{
uint32_t _32_bits;

int v1= _telem_callsign[1]; if (v1<65) v1=v1-'0'; else v1=v1+10-'A';  
int v2= _telem_callsign[3]-'A'; 
int v3= _telem_callsign[4]-'A'; 
int v4= _telem_callsign[5]-'A'; 

_32_bits=v1; 
_32_bits *= 26;_32_bits+=v2; 
_32_bits *= 26;_32_bits+=v3; 
_32_bits *= 26;_32_bits+=v4; 

altitude=20*(_32_bits%1068);  _32_bits=_32_bits/1068;
grid6='A'+(_32_bits%24); _32_bits=_32_bits/24;
grid5='A'+(_32_bits%24);

snprintf(grid, 7,"%s%c%c",_4chargrid,grid5,grid6);
maidenhead_to_latlon(grid, &lat, &lon);	
}
////********************************
void get_iso_utc_time(char *buffer, size_t size) {
    struct timeval tv;
    struct tm *tm_info;
    gettimeofday(&tv, NULL);  // Get current time with microseconds
    tm_info = gmtime(&tv.tv_sec);  // Convert to UTC
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", tm_info); // Format datetime
    snprintf(buffer + 19, size - 19, ".%03ldZ", tv.tv_usec / 1000); // Add milliseconds
}
//***************************************************************************
void send_to_sondehub(void)  //via json payload
{
	const char *url = "http://api.v2.sondehub.org/amateur/telemetry";
	char datetime[30];
    get_iso_utc_time(datetime, sizeof(datetime));

	char json_payload[501];

	snprintf(json_payload, 500,"[{"
    "\"software_name\":\"twits\","
    "\"software_version\":\"0.9\","
	"\"datetime\":\"%s\","
	"\"comment\":\"%s\","
	"\"detail\":\"%s\","
    "\"uploader_callsign\":\"%s\","
    "\"payload_callsign\":\"%s\","
    "\"lat\":%f,"
    "\"lon\":%f,"
    "\"alt\":%d"
    "}]",datetime,comment,detail,_uploader,callsign,lat,lon,altitude);           

			printf("\nJason Payload is: %s\n",json_payload);
			fprintf(log_file,"Jason Payload is: %s\n",json_payload);

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
    res = curl_easy_perform(curl);

}

//***************************************************************************
int main(int argc, char *argv[]) {
			log_file = fopen("twits_log.txt","a");  
	epoch_time = time(NULL); //      printf("Epoch time: %ld\n", epoch_time);
	curl = curl_easy_init();
			printf("arg count: %d, callsio: %s arg 2 start minute: %s arg3 id1: %s arg4 id3: %s\n",argc,argv[1],argv[2],argv[3],argv[4]); //callsign, minute, id1, id3
			fprintf(log_file, "epoch: %d arg count: %d, callsio: %s arg 2 start minute: %s arg3 id1: %s arg4 id3: %s\n",epoch_time,argc,argv[1],argv[2],argv[3],argv[4]); 
	snprintf(callsign, 7, "%s",argv[1]);
	if (argc >5) snprintf(comment,99,"%s",argv[5]); else  snprintf(comment,99,"generic comment");
	if (argc >6) snprintf(detail,99,"%s",argv[6]); else  snprintf(detail,99,"generic detail");


    // Build query string for callsign packet from wspr.live
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT * FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (tx_sign LIKE '%s') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),argv[1]);
	send_query_and_write_reponse_to_file();
	process_1st_packet();
	
    // Build query string for telemetry packet from wspr.live
	int start_minute_of_telem_packet = (atoi(argv[2])+2)%10;
	snprintf(query_raw, sizeof(query_raw),"db1.wspr.live/?query=SELECT toString(time) as stime, band, tx_sign, tx_loc, tx_lat, tx_lon, power, stime FROM wspr.rx WHERE (band='14') AND (time >%ld) AND (stime LIKE '____-__-__ __%%3A_%d%%25') AND (tx_sign LIKE '%s_%s%%25') ORDER BY time DESC LIMIT 1",(epoch_time-SECONDS_TO_LOOK_BACK),start_minute_of_telem_packet,argv[3],argv[4]);
	send_query_and_write_reponse_to_file();	
	process_2nd_packet();
	decode_telem_data();    //extracts the encoded info from telemetry packet (grid chars 5,6 and altitude) and converts grid to lat/lon
if 	(packet_count==2)
	send_to_sondehub();     // sends via json 

	curl_easy_cleanup(curl);      //only do after ALL your curling is done
	curl_global_cleanup();
	fprintf(log_file,"\n\n");
	fclose(log_file);
    return 0;
}








