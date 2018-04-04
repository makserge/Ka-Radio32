/*
 * Copyright 2016 karawin (http://www.karawin.fr)
*/
#define TAG "webclient"
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#include "webclient.h"
#include "webserver.h"
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/netdb.h"

#include "freertos/semphr.h"

#include "vs1053.h"
#include "eeprom.h"
#include "interface.h"
#include "gpio.h"
#include "audio_player.h"
#include "spiram_fifo.h"
#include "app_main.h"

extern player_t* player_config;
#define min(a, b) (((a) < (b)) ? (a) : (b))

enum clientStatus cstatus;
//static uint32_t metacount = 0;
//static uint16_t metasize = 0;

extern bool ledStatus;

xSemaphoreHandle sConnect, sConnected, sDisconnect, sHeader;

static uint8_t once = 0;
static uint8_t playing = 0;

static const char* icyHeaders[] = { "icy-name:", "icy-notice1:", "icy-notice2:",  "icy-url:", "icy-genre:", "icy-br:","icy-description:","ice-audio-info:", "icy-metaint:" };
contentType_t contentType;

static char notfound[]={"Not Found"};
static char parEmpty[] = {" "};
const char CLIPLAY[]  = {"##CLI.PLAYING#%c%c"};
const char CLISTOP[]  = {"##CLI.STOPPED# from %s\n"};

const char strcMALLOC[]  = {"Client: incmalloc fails for %d\n"};
const char strcMALLOC1[]  = {"%s malloc fails\n"};
const char strcWEBSOCKET[]  = {"WebClient webSocket fails %s errno: %d\n"};
const char strcSOCKET[]  = {"WebClient Socket fails %s errno: %d\n"};

/* TODO:
	- METADATA HANDLING
	- IP SETTINGS
	- VS1053 - DELAY USING vTaskDelay
*/
static struct icyHeader header = {{{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL}}};

static char metaint[10];
static char clientURL[256]= {0,0};
static char clientPath[256] = {0,0};
static uint16_t clientPort = 80;


static struct hostent *server = NULL;


void *incmalloc(size_t n)
{
	void* ret;	
//printf ("Client malloc of %d %d,  Heap size: %d\n",n,((n / 32) + 1) * 32,xPortGetFreeHeapSize( ));
	ret = malloc(n);
	if (ret == NULL) printf(strcMALLOC,n);
//	if (n <4) printf("Client: incmalloc size:%d\n",n);	
	ESP_LOGV(TAG,"Client malloc after of %d bytes ret:%x  Heap size: %d",n,(int)ret,xPortGetFreeHeapSize( ));
	return ret;
}	
void incfree(void *p,char* from)
{
	if (p != NULL) free(p);
//	else printf ("Client incfree from %s NULL\n",from);
	ESP_LOGV(TAG,"Client incfree of %x, from %s           Heap size: %d",(int)p,from,xPortGetFreeHeapSize( ));
}	




bool getState()
{
	 return playing;
}

void clientPrintState()
{
	if (playing)
		kprintf(CLIPLAY,0x0d,0x0a);
	else
		kprintf(CLISTOP,"State");
}
void clientInit() {
	vSemaphoreCreateBinary(sHeader);
	vSemaphoreCreateBinary(sConnect);
	vSemaphoreCreateBinary(sConnected);
	vSemaphoreCreateBinary(sDisconnect);
	xSemaphoreTake(sConnect, portMAX_DELAY);
	xSemaphoreTake(sConnected, portMAX_DELAY);
	xSemaphoreTake(sDisconnect, portMAX_DELAY);
}

uint8_t clientIsConnected() {
	if(xSemaphoreTake(sConnected, 0)) {
		xSemaphoreGive(sConnected);
		return 0;
	}
	return 1;
}

// for debug only

void dump(uint8_t* from, uint32_t len )
{
	uint32_t i ;
	uint8_t* addr ;
	addr =  from;
	for (i=0;i<len;i+=16){
		printf("\n%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x    \t\
		%c %c %c %c %c %c %c %c %c %c %c %c %c %c %c %c",(unsigned int)addr,(unsigned int)addr[0],(unsigned int)addr[1],(unsigned int)addr[2],(unsigned int)addr[3],(unsigned int)addr[4],(unsigned int)addr[5],(unsigned int)addr[6],(unsigned int)addr[7],(unsigned int)addr[8],(unsigned int)addr[9],(unsigned int)addr[10],(unsigned int)addr[11],(unsigned int)addr[12],(unsigned int)addr[13],(unsigned int)addr[14],(unsigned int)addr[15],addr[0],addr[1],addr[2],addr[3],addr[4],addr[5],addr[6],addr[7],addr[8],addr[9],addr[10],addr[11],addr[12],addr[13],addr[14],addr[15]);
		addr+=16;
	}	
	printf("\n");
}

struct icyHeader* clientGetHeader()
{	
	return &header;
}

	
bool clientParsePlaylist(char* s)
{
  char* str;
  char* ns; 
  char path[255] = "/";
  char url[78]; 
  char port[5] = "80";
  int remove = 0;
  int i = 0; int j = 0;
  
// for extm3u skip line with #EXTINF  
  str = strstr(s,"#EXTINF");
  if (str != NULL) //skip to next line
  {
	ns = str;
    while ((strlen(ns) > 1) && (ns[0]!=0x0A)) ns++;
	ESP_LOGV(TAG,"EXTM3U: %s",ns);
	s= ns;
  }
  str = strstr(s,"<location>http://");  //for xspf
  if (str != NULL) remove = 17;
  
  if (str ==NULL) 
  {	
	str = strstr(s,"<REF href = \"http://");  //for asx
	if (str != NULL) remove = 20;
  }
  if (str ==NULL) 
  {	  
	str = strstr(s,"http://");
	if (str != NULL) remove = 7;
	else
	{
		str = strstr(s,"HTTP://");
		if (str != NULL) remove = 7;
	}
  }
  if (str ==NULL) 
  {	  
	str = strstr(s,"https://");
	if (str != NULL) remove = 8;
	else
	{
		str = strstr(s,"HTTPS://");
		if (str != NULL) remove = 8;
	}
  } 
  
  if (str != NULL)   
  {
	str += remove; //skip http://	
	ESP_LOGV(TAG,"parse str %s",str);
	
	while ((str[i] != '/')&&(str[i] != ':')&&(str[i] != 0x0a)&&(str[i] != 0x0d)&&(j<78)) {url[j] = str[i]; i++ ;j++;}
	url[j] = 0;
	ESP_LOGV(TAG,"parse str url %s",url);
	j = 0;
	if (str[i] == ':')  //port
	{
		i++;
		while ((str[i] != '/')&&(str[i] != 0x0a)&&(str[i] != 0x0d)) {port[j] = str[i]; i++ ;j++;}
	}
	j = 0;
	if ((str[i] != 0x0a)&&(str[i] != 0x0d)&&(str[i] != 0)&&(str[i] != '"')&&(str[i] != '<'))
	{	
	  while ((str[i] != 0x0a)&&(str[i] != 0x0d)&&(str[i] != 0)&&(str[i] != '"')&&(str[i] != '<')&&(j<255)) {path[j] = str[i]; i++; j++;}
	  path[j] = 0;
	}
	
	
	
	if (strncmp(url,"localhost",9)!=0) clientSetURL(url);
	clientSetPath(path);
	clientSetPort(atoi(port));
//kprintf("##CLI.URL#: %s, path: %s, port: %s\n",url,path,port);
	return true;
  }
  else 
  { 
   cstatus = C_DATA;
   return false;
  }
}
//---------------------------------------
static char* stringify(char* str,int len)
{
//		if ((strchr(str,'"') == NULL)&&(strchr(str,'/') == NULL)) return str;
        if (len == 0) return str;
		char* new = incmalloc(len+20);
		int nlen = len+20;
		if (new != NULL)
		{
			ESP_LOGV(TAG,"stringify: enter: len:%d  \"%s\"",len,str);
			int i=0 ,j =0;
			for (i = 0;i< len+10;i++) new[i] = 0;
			for (i=0;i< len;i++)
			{
				if (str[i] == '"') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				} else
				if (str[i] == '/') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				}
				else				
				if (str[i] == '\\') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				}				
				else	// pseudo ansi utf8 convertion
					if ((str[i] > 192) && (str[i+1] < 0x80)){ // 128 = 0x80
					new[j++] = 195; // 192 = 0xC0   195 = 0xC3
					new[j++] =(str)[i]-64 ; // 64 = 0x40
				} 
				else new[j++] =(str)[i] ;
				
				if ( j+20> nlen) 
				{
					nlen +=20;
					new = realloc(new,nlen); // some room
				}
			}
			incfree(str,"str");

			new = realloc(new,j+1); // adjust
			ESP_LOGV(TAG,"stringify: exit: len:%d  \"%s\"",j,new);
			return new;		
		} else 
		{
			printf(strcMALLOC1,"stringify");
		}	
		return str;
}

bool clientPrintMeta()
{
	if (header.members.mArr[METADATA] != NULL)
		kprintf("##CLI.META#: %s\n",header.members.mArr[METADATA]);
	else
		kprintf("##CLI.META#:%c", 0x0D);
	return true;
}

char* getMeta()
{
	return (header.members.mArr[METADATA] == NULL)?parEmpty:header.members.mArr[METADATA];
}

static void removePartOfString(char* origine, char* remove)
{
	if (strlen(origine) == 0) return;
	char* copy = incmalloc(strlen(origine));
	char* t_end;
	if (copy != NULL)
	{
		while ( (t_end = strstr(origine,remove))!= NULL)
		{
			*t_end = 0;
			strcpy(copy,origine);
			strcat(copy,t_end+(strlen(remove)));
			strcpy(origine,copy);
		}	
		incfree(copy,"removePt");
	}
}

// A metadata found. Extract the Stream title
static void clientSaveMetadata(char* s,int len)
{
		char* t_end = NULL;
		char* t ,*tt;
		bool found = false;
		if ((len == 0)||(s==NULL)) printf("clientSaveMetadata:  len:%d\n",len); 
		if ((len > 256) ||(s == NULL) || (len == 0))
		{
			if (header.members.mArr[METADATA] != NULL)
			incfree(header.members.mArr[METADATA],"metad");
			header.members.mArr[METADATA] = NULL;
			return;
		}
		t = s;
		len = strlen(t);
		ESP_LOGV(TAG,"clientSaveMetadata:  len:%d   char:%s",len,s);
		t_end = strstr(t,"song_spot=");
		if (t_end != NULL)
		{ 
			*t_end = 0;
			found = true;
			removePartOfString(t, "text=");
			removePartOfString(t, "\"");
		}
		else
		{
			t_end = strstr(t,";StreamUrl='");
			if (t_end != NULL) 
			{
				*t_end = 0;found = true;
			} 
		}			
		t = strstr(t,"StreamTitle='");
		if (t!= NULL) {t += 13;found = true;} else t = s;
		len = strlen(t);
		if ((t_end != NULL)&&(len >=3)) t_end -= 3;
		else {
			if (t_end != NULL) t_end -=1;
			else
			if (len >=2) {t_end = t+len-2;found = true;} 
			else t_end = t+len;
		}

		if (found)
		{	
			t_end = strstr(t_end,"'");
			if (t_end !=NULL)
			*t_end = 0;
		
			if (t!=NULL)
			{
				t_end = strstr(t,"||");
				if (t_end !=NULL)
				*t_end = 0;
			}	
			
		}
		else
		{
			if (len >=2) len-=2; 
		}

		ESP_LOGV(TAG,"clientSaveMetadata0:  len:%d   char:%s",strlen(t),t);

// see if it is !=
		tt = NULL;
		if (t != NULL) 
		{ 
			tt = incmalloc((len+3)*sizeof(char));
			if (tt != NULL)
			{
				strcpy(tt,t);
				tt = stringify(tt,len); 
			}
		}


	if  ((header.members.mArr[METADATA] == NULL)||((header.members.mArr[METADATA] != NULL)&&(t!= NULL)&&(strcmp(tt,header.members.mArr[METADATA]) != 0)))
	{

		// ******* Приведение букв в кодировку UTF-8 **********
		// (c) alex08cb@mail.ru Aleksey Shevchenko aka oka275
		// ****************************************************/
		ESP_LOGD(TAG,"UTF-8conv: start work, RAM left:%d\n",esp_get_free_heap_size()); //Сообщение в дебаг для отладки

		char* b = incmalloc(256); //Запросили выделением памяти под временный буфер, в котором будем собирать строку.
		if (b != NULL) //Если память выделилась
		{
			char char_acsii[2] = { '0', '\0' };  //Буферок под простую ASCII-букву
			char char_utf[3] = { '0','0','\0' }; //Буферок под UTF8-букву
			int index,tLen;
			unsigned char chr;
			bool cp1251fnd = false;
			chr=index=0; 		//Инициировали переменные
			strcpy(b, "");		// -//-
			tLen = strlen(t); 	//Получили длину входящего массива

			while (index < tLen)  //Организуем цикл. Пока входящая строка не закончится, будем перебирать её побайтно.
			{
				chr = t[index]; //Загружаем в переменную код буквы
				if(chr < 127)	//Если она меньше 127, то это обычная латинская буква, цифра или знак препинания.
				{
					char_acsii[0]=chr;//Просто перекладываем ее в буферок
					strcat(b, char_acsii); //и ДОБАВЛЯЕМ символ в выходной буфер как очень короткое слово в выходной буфер. Признак конца строки конечно тоже переносится (см.команду strcat)
				}
				else //Если код символа больше 127 (будем надеяться, что на станции сидят вменяемые люди и не будут пользоваться нестандартными символами)
				{
					if((chr == 0xD0)||(chr == 0xD1)) //Если встречаем в строке код D0 или D1, то это может быть или буква Р или С в CP-1251, или первый символ пары UTF8
					{
						if((index+1)>=tLen)cp1251fnd=true; //если это последний символ в строке, то это однозначно CP-1251 (или битый пакет, но мы оптимисты)
						else  //Если это символ не последний в строке, то по закону кодировки UTF8 следующий символ должен быть в диапазоне 0x80-0xBF...
						{
							if( (t[index+1] >= 0x80) && (t[index+1] <= 0xBF) ) //И если это так, то это UTF-8 кодировка, родная для "Ka-Radio".
							{
								char_utf[0]=t[index++]; //Кладем первый байт пары (D0 или D1)
								char_utf[1]=t[index];	//Кладет второй байт пары (собственно, сам символ)
								strcat(b, char_utf);	//ДОБАВЛЯЕМ символ в выходной буфер как очень короткое слово.
							}
							else cp1251fnd=true;		//Если же символ не попадал в диапазон (а его код может быть только больше этого диапазона), то это CP-1251
						}
					}
					else	cp1251fnd=true; //Если символ сразу не попал в диапазон D0-D1, то это кодировка CP-1251

					if(cp1251fnd)  //Если флаг установлен, то перекодируем символ
					{
						chr=chr-48; //Большинство кодов символов CP-1251 больше кодов UTF-8 ровно на число 48, за небольшим исключением. Вычитаем!
						if(chr > 0xBF) //Если получился код больше числа 0xBF, то эта буква находится в следующем банке 0xD1 и разница увеличивается еще на число 64 (всего 112)
						{
						char_utf[0]=0xD1;	 //Кладем первый байт пары (D1)
						char_utf[1]=chr-64;  //Вычитаем дополнительно цифру 64 и получаем код символа в UTF-8. Кладем второй байт пары.
						}
						else
						{
						char_utf[0]=0xD0;	//Если символ не превышал 0xBF, то значит он из первого банка 0xD0
						char_utf[1]=chr;    //Кладем второй байт без изменений (48 мы уже вычли выше)
						}
						strcat(b, char_utf); //ДОБАВЛЯЕМ символ в выходной буфер как очень короткое слово.
					}
				}
				index++; //Увеличиваем индекс-указатель на следующий код символа во входном буфере.
				cp1251fnd = false; //Сбрасываем флаг
			}
			len = strlen(b); //Получаем размер выходного буфера
			if(len > 256) {ESP_LOGD(TAG,"UTF-8conv: string over 256 bytes!" ); incfree(b,"UTF-8 b-overload");return; }
			//Окончание перекодировки.
			ESP_LOGD(TAG,"UTF-8conv: work complete, tag=%s RAM left:%d\n",b,esp_get_free_heap_size()); //Сообщение в дебаг для отладки
		}
		else //Если память не выделилалсь
		{
			printf(strcMALLOC1,"utf8_normalize"); //Сообщаем
			return; //Застреливаемся.
	    }


		if (header.members.mArr[METADATA] != NULL) incfree(header.members.mArr[METADATA],"metad");
		header.members.mArr[METADATA] = (char*)incmalloc((len+3)*sizeof(char));
		if(header.members.mArr[METADATA] == NULL) //Если память не выделилась, то сообщаем и выходим.
		{	printf(strcMALLOC1,"metad");
			incfree(tt,"");
			return;
		}

		strcpy(header.members.mArr[METADATA], b);
		//	dump((uint8_t*)(header.members.mArr[METADATA]),strlen(header.members.mArr[METADATA]));
		header.members.mArr[METADATA] = stringify(header.members.mArr[METADATA],len);
		//	dump((uint8_t*)(header.members.mArr[METADATA]),strlen(header.members.mArr[METADATA]));

		clientPrintMeta();
		ESP_LOGD(TAG,"start blank, RAM left:%d\n",esp_get_free_heap_size()); //Сообщение в дебаг для отладки
		while ((header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == ' ')||
			(header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == '\r')||
		(header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == '\n')
		)
		{
			header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] = 0; // avoid blank at end
		}
		incfree(b,"b-buffer!");
		ESP_LOGD(TAG,"blank complete, RAM left:%d\n",esp_get_free_heap_size()); //Сообщение в дебаг для отладки
		// send station name if no metadata
		if (strlen(header.members.mArr[METADATA])!=0)
			t_end = header.members.mArr[METADATA];
		else
			t_end = (header.members.single.name ==NULL)?"":header.members.single.name;

		char* title = incmalloc(strlen(t_end)+15);
		if (title != NULL)
		{
			sprintf(title,"{\"meta\":\"%s\"}",t_end);
			websocketbroadcast(title, strlen(title));
			incfree(title,"title");
		} else printf(strcMALLOC1,"Title");
	}
		incfree(tt,"");
}	

// websocket: next station
void wsStationNext()
{
	struct shoutcast_info* si =NULL;
	do {
		setCurrentStation(getCurrentStation()+1);
		if (getCurrentStation() >= 255)
			setCurrentStation(0);
		si = getStation(getCurrentStation());	
	}
	while (si == NULL || ((si != NULL)&&(strcmp(si->domain,"")==0)) || ((si != NULL)&&(strcmp( si->file,"")== 0)));

	playStationInt(getCurrentStation());
	incfree(si,"wsstation");
}
// websocket: previous station
void wsStationPrev()
{
	struct shoutcast_info* si = NULL;
	do {
		if (getCurrentStation() >0)
		{				
			setCurrentStation(getCurrentStation()-1);
			si = getStation(getCurrentStation());
		}	
		else return;
	}
	while (si == NULL || ((si != NULL)&&(strcmp(si->domain,"")==0)) || ((si != NULL)&&(strcmp( si->file,"")== 0)));

	playStationInt	(getCurrentStation());
	incfree(si,"wsstation");
}

// websocket: broadcast volume to all client
void wsVol(char* vol)
{
	char answer[21];
	if (vol != NULL)
	{	
		sprintf(answer,"{\"wsvol\":\"%s\"}",vol);
		websocketbroadcast(answer, strlen(answer));
	} 
}	
// websocket: broadcast monitor url
void wsMonitor()
{
		char answer[300];
		memset(answer,0,300);
		if ((clientPath[0]!= 0))
		{
			sprintf(answer,"{\"monitor\":\"http://%s:%d%s\"}",clientURL,clientPort,clientPath);
			websocketbroadcast(answer, strlen(answer));
		}
}						
//websocket: broadcast all icy and meta info to web client.
static void wsHeaders()
{
//remove	uint8_t header_num;
	char currentSt[6]; 	
	sprintf(currentSt,"%d",getCurrentStation());
	char* not2;
	not2 = header.members.single.notice2;
	if (not2 ==NULL) not2=header.members.single.audioinfo;
	if ((header.members.single.notice2 != NULL)&&(strlen(header.members.single.notice2)==0)) not2=header.members.single.audioinfo;
	int json_length ;
	json_length =104+ //93
		strlen(currentSt)+
		((header.members.single.description ==NULL)?0:strlen(header.members.single.description)) +
		((header.members.single.name ==NULL)?0:strlen(header.members.single.name)) +
		((header.members.single.bitrate ==NULL)?0:strlen(header.members.single.bitrate)) +
		((header.members.single.url ==NULL)?0:strlen(header.members.single.url))+ 
		((header.members.single.notice1 ==NULL)?0:strlen(header.members.single.notice1))+
		((not2 ==NULL)?0:strlen(not2))+
		((header.members.single.genre ==NULL)?0:strlen(header.members.single.genre))+
		((header.members.single.metadata ==NULL)?0:strlen(header.members.single.metadata))
		;
	char* wsh = incmalloc(json_length+1);
	if (wsh == NULL) {printf(strcMALLOC1,"wsHeader");return;}
	sprintf(wsh,"{\"wsicy\":{\"curst\":\"%s\",\"descr\":\"%s\",\"meta\":\"%s\",\"name\":\"%s\",\"bitr\":\"%s\",\"url1\":\"%s\",\"not1\":\"%s\",\"not2\":\"%s\",\"genre\":\"%s\"}}",
			currentSt,
			(header.members.single.description ==NULL)?"":header.members.single.description,
			(header.members.single.metadata ==NULL)?"":header.members.single.metadata,	
			(header.members.single.name ==NULL)?"":header.members.single.name,
			(header.members.single.bitrate ==NULL)?"":header.members.single.bitrate,
			(header.members.single.url ==NULL)?"":header.members.single.url,
			(header.members.single.notice1 ==NULL)?"":header.members.single.notice1,
			(not2 ==NULL)?"":not2 ,
			(header.members.single.genre ==NULL)?"":header.members.single.genre); 
	ESP_LOGV(TAG,"WSH: len:%d  \"%s\"",strlen(wsh),wsh);
	websocketbroadcast(wsh, strlen(wsh));	
	incfree (wsh,"wsh");
}	

//Clear all ICY and META infos

static void clearHeaders()
{
	uint8_t header_num;
	for(header_num=0; header_num<ICY_HEADER_COUNT; header_num++) {
		if(header_num != METAINT) 
			if(header.members.mArr[header_num] != NULL) {
				incfree(header.members.mArr[header_num],"header" );
//				header.members.mArr[header_num][0] = 0;				
				header.members.mArr[header_num] = NULL;				
			}
	}
	header.members.mArr[METAINT] = 0;
	wsHeaders();
}
	
bool clientPrintOneHeader(uint8_t header_num)
{
	if (header.members.mArr[header_num] != NULL)
	kprintf("##CLI.ICY%d#: %s\n",header_num,header.members.mArr[header_num]);
return true;
}

bool clientPrintHeaders()
{
	uint8_t header_num;
	for(header_num=0; header_num<ICY_HEADER_COUNT; header_num++) {
		if((header_num != METAINT) && (header_num != METADATA))
			if(header.members.mArr[header_num] != NULL) {
				kprintf("##CLI.ICY%d#: %s\n",header_num,header.members.mArr[header_num]);
			}	
	}
	clientPrintMeta();	
	return true;
}	

static bool clientSaveOneHeader(char* t, uint16_t len, uint8_t header_num)
{
	char* tt;
	if(header.members.mArr[header_num] != NULL) 
		incfree(header.members.mArr[header_num],"headernum");
	tt = incmalloc((len+1)*sizeof(char));
	if(tt == NULL)
	{
		printf(strcMALLOC1,"clientSOneH");
		return false;
	}	
	
	int i;
	for(i = 0; i<len+1; i++) tt[i] = 0;
	strncpy(tt, t, len);
//	header.members.mArr[header_num] = stringify(header.members.mArr[header_num],len);
	header.members.mArr[header_num] = stringify(tt,len); //tt is freed here
	vTaskDelay(10);
	clientPrintOneHeader(header_num);
	ESP_LOGV(TAG,"header after num:%d addr:0x%x  cont:\"%s\"",header_num,(int)header.members.mArr[header_num],header.members.mArr[header_num]);
	return true;
}

	
bool clientParseHeader(char* s)
{
	// icy-notice1 icy-notice2 icy-name icy-genre icy-url icy-br
	uint8_t header_num;
	char *t;
	bool ret = false;
	ESP_LOGV(TAG,"ParseHeader: %s",s);
	xSemaphoreTake(sHeader,portMAX_DELAY);
	if ((cstatus != C_HEADER1)&& (cstatus != C_PLAYLIST))// not ended. dont clear
	{
		clearHeaders();
	}	

		t = strstr(s,"Content-Type:");
		if (t == NULL) t = strstr(s,"content-type:");
		if (t != NULL)
		{
			contentType = KMIME_UNKNOWN;
			if (strstr(t, "application/octet-stream")) contentType = KOCTET_STREAM;
			if (strstr(t, "audio/aac")) contentType = KAUDIO_AAC;
			if (strstr(t, "audio/mp4")) contentType = KAUDIO_MP4;
			if (strstr(t, "audio/x-m4a")) contentType = KAUDIO_MP4;
			if (strstr(t, "audio/mpeg")) contentType = KAUDIO_MPEG;

			if(contentType == KMIME_UNKNOWN) {
				ESP_LOGD(TAG, "unknown contentType: %s", t);
			}	
			ESP_LOGD(TAG, "contentType: %d", contentType);
			player_config->media_stream->content_type = contentType;
		}


	
	for(header_num=0; header_num<ICY_HEADERS_COUNT; header_num++)
	{
		ESP_LOGV(TAG,"icy deb: %d",header_num);				
		t = strstr(s, icyHeaders[header_num]);
		if( t != NULL )
		{
			t += strlen(icyHeaders[header_num]);
			char *t_end = strstr(t, "\r\n");
			if(t_end != NULL)
			{
				ESP_LOGV(TAG,"icy in: %d",header_num);		
				uint16_t len = t_end - t;
				if(header_num != METAINT) // Text header field
				{
					ret = clientSaveOneHeader(t, len, header_num);
				}
				else // Numerical header field
				{					
						int i;
						for(i = 0; i<len+1; i++) metaint[i] = 0;
						strncpy(metaint, t, len);
						header.members.single.metaint = atoi(metaint);
						ESP_LOGV(TAG,"len = %d,MetaInt= %s, Metaint= %d",len, metaint,header.members.single.metaint);
						ret = true;
						ESP_LOGV(TAG,"icy: %s, %d",icyHeaders[header_num],header.members.single.metaint);					
				}
			}
		}
	}
	if (ret == true) 
	{
		wsHeaders();
		wsMonitor();
	}
	xSemaphoreGive(sHeader);
		return ret;
}


void clientSetName(char* name,uint16_t index)
{
	kprintf("##CLI.NAMESET#: %d %s\n",index,name);
}

void clientSetURL(char* url)
{
//remove	int l = strlen(url)+1;
	if (url[0] == 0xff) return; // wrong url
	strcpy(clientURL, url);
	kprintf("##CLI.URLSET#: %s\n",clientURL);
}

void clientSetPath(char* path)
{
//remove	int l = strlen(path)+1;
	if (path[0] == 0xff) return; // wrong path
	strcpy(clientPath, path);
	kprintf("##CLI.PATHSET#: %s\n",clientPath);
}

void clientSetPort(uint16_t port)
{
	clientPort = port;
	kprintf("##CLI.PORTSET#: %d\n",port);
}


void clientConnect()
{
	cstatus = C_HEADER;
	once = 0;
	if((server = (struct hostent*)gethostbyname(clientURL))) {
		xSemaphoreGive(sConnect);
	} else {
		clientDisconnect("clientConnect");
	}
}
void clientConnectOnce()
{
	cstatus = C_HEADER;
	if((server = (struct hostent*)gethostbyname(clientURL))) {
		xSemaphoreGive(sConnect);
	} else {
		clientDisconnect("clientConnectOnce");
	}
	once = 1; // play one time
}
void clientSilentConnect()
{
	cstatus = C_HEADER;
	once = 0;
	if(server != NULL) {
		xSemaphoreGive(sConnect);
	} else {
		clientSilentDisconnect();
	}
}
void clientSilentDisconnect()
{
	xSemaphoreGive(sDisconnect);
	audio_player_stop();
	for (int i = 0;i<100;i++)
	{
		if(!clientIsConnected())break;
		vTaskDelay(1);
	}	
	
}

void clientDisconnect(const char* from)
{
	kprintf(CLISTOP,from);
	xSemaphoreGive(sDisconnect);
	audio_player_stop();
	for (int i = 0;i<100;i++)
	{
		if(!clientIsConnected())break;
		vTaskDelay(1);
	}	
	if ((from[0]!='C') || (from[1]!='_'))
		if (!ledStatus) gpio_set_level(getLedGpio(),0);
	vTaskDelay(6);
}

IRAM_ATTR void clientReceiveCallback(int sockfd, char *pdata, int len)
{
	static int metad ;
	static int rest ;
	static uint32_t chunked;
	static uint32_t cchunk;
	static char* metadata = NULL;
	uint16_t l =0;
	uint32_t lc;
	char *inpdata;
	char* inpchr;
	uint32_t clen;
	char* t1;
	char* t2;
	bool  icyfound;

//	if (cstatus != C_DATA) {printf("cstatus= %d\n",cstatus);  printf("Len=%d, Byte_list = %s\n",len,pdata);}
	if (cstatus != C_DATA)
	{
		t1 = strstr(pdata, "404"); 
		if (t1 != NULL)
		{
			t1 = strstr(pdata, notfound); 
			if (t1 == NULL) t1 = strstr(pdata,"not be found");
		}
		if (t1 != NULL) { // 
			kprintf(CLIPLAY,0x0d,0x0a);
//			kprintf("\n");
			clientSaveOneHeader(notfound, 13,METANAME);
			wsHeaders();
//			vTaskDelay(200);
//			clientDisconnect("C_DATA");
			cstatus = C_HEADER;
			return;
		}	
	}	
	switch (cstatus)
	{
	case C_PLAYLIST:
         if (!clientParsePlaylist(pdata)) //need more
		  cstatus = C_PLAYLIST1;
		else {clientDisconnect("C_PLIST");  }
    break;
	case C_PLAYLIST1:
       clientDisconnect("C_PLIST1");	   
        clientParsePlaylist(pdata) ;//more?
		cstatus = C_PLAYLIST;
	break;
	case C_HEADER0:
	case C_HEADER:
		clearHeaders();
		metad = -1;	
		t1 = strstr(pdata, "302 "); 
		if (t1 ==NULL) t1 = strstr(pdata, "301 "); 
		if (t1 != NULL) { // moved to a new address
			if( strcmp(t1,"Found")||strcmp(t1,"Temporarily")||strcmp(t1,"Moved"))
			{
				ESP_LOGV(TAG,"Header Len=%d,\n %s",len,pdata);
				kprintf("Header: Moved\n");
				clientDisconnect("C_HDER");
				clientParsePlaylist(pdata);
				cstatus = C_PLAYLIST;	
				 
			}	
			break;
		}
		//no break here
	case C_HEADER1:  // not ended
		{
			cstatus = C_HEADER1;
			do {
				t1 = strstr(pdata, "\r\n\r\n"); // END OF HEADER
				ESP_LOGV(TAG,"Header1 len: %d,  Header: %s",len,pdata);
				if ((t1 != NULL) && (t1 <= pdata+len-4)) 
				{
						t2 = strstr(pdata, "Internal Server Error"); 
						if (t2 != NULL)
						{
							printf("Internal Server Error%c",0x0d);
							clientDisconnect("Internal Server Error");
							cstatus = C_HEADER;
							
						}
						icyfound = 	clientParseHeader(pdata);
						wsMonitor();											
/*						if(header.members.single.bitrate != NULL) 
							if (strcmp(header.members.single.bitrate,"320")==0)
								 system_update_cpu_freq(SYS_CPU_160MHZ);
							else system_update_cpu_freq(SYS_CPU_80MHZ);*/
						if(header.members.single.metaint > 0) 
							metad = header.members.single.metaint;
						ESP_LOGD(TAG,"t1: 0x%x, cstatus: %d, icyfound: %d  metad:%d Metaint:%d\n", (int) t1,cstatus, icyfound,metad,  (header.members.single.metaint)); 
						cstatus = C_DATA;	// a stream found
						
/////////////////////////////////////////////////////////////////////////////////////////////////						
						player_config->media_stream->eof = false;
						audio_player_start();
/////////////////////////////////////////////////////////////////////////////////////////////////						
						
						
//						VS1053_flush_cancel(0);
//						VS1053_flush_cancel(1);
						t2 = strstr(pdata, "Transfer-Encoding: chunked"); // chunked stream? 
//						t2 = NULL;
						chunked = 0;
						t1+= 4; 
						if ( t2 != NULL) 
						{
							while (len -(t1-pdata)<8) {len += recv(sockfd, pdata+len, RECEIVE+8-len, 0); }
							chunked = (uint32_t) strtol(t1, NULL, 16) +2;
							if (strchr((t1),0x0A) != NULL)
								*strchr(t1,0x0A) = 0;
							
							ESP_LOGD(TAG,"chunked: %d,  strlen: %d  \"%s\"",chunked,strlen(t1)+1,t1);
							t1 +=strlen(t1)+1; //+1 for char 0, 
						}
						
						int newlen = len - (t1-pdata) ;
						cchunk = chunked;
						ESP_LOGD(TAG,"newlen: %d   len: %d   chunked:%d  pdata:%x",newlen,len,chunked,(int)pdata);
						if(newlen > 0) clientReceiveCallback(sockfd,t1, newlen);
				} else
				{
					t1 = NULL;
					len += recvfrom(sockfd, pdata+len, RECEIVE-len, 0,NULL,NULL);
				}
			} while (t1 == NULL);
		}
	break;
	default:		
// -----------	

// Chunk computing
		lc = len; // lc rest after chunk
//	 printf("CDATAIN: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
		if((chunked != 0)&&((cchunk ==0)||(len >= cchunk-1)))  //if in chunked mode and chunk received or complete in data
		{
//	 printf("CDATA1: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
			if (len == cchunk) // if a complete chunk in pdata, remove crlf
			{ 
				len -= 2;
				cchunk = 0;
//	printf("lenoe:%d, chunked:%d  cchunk:%d, lc:%d, metad:%d\n",len,chunked,cchunk, lc,metad );
			} else  // an incomplete chunk in progress
			{	
				if (len == cchunk-1) // missing lf: remove cr only, wait lf in next data
				{ 
					len -= 1;
					cchunk = 1;
//	printf("leno1:%d, chunked:%d  cchunk:%d, lc:%d, metad:%d\n",len,chunked,cchunk, lc,metad );
				} 				
				else		// a part of end of chunk 	and beginnining of a new one
				{
					inpdata = pdata;
					
					while (lc != 0)
					{					
						while (lc < cchunk+9) 
						{
							clen = recvfrom(sockfd, pdata+len, 9, 0,NULL,NULL); 
							lc+=clen;len+=clen;
							ESP_LOGV(TAG,"more:%d, lc:%d\n",clen,lc);
						} //security to be sure to receive the new length
						
//	printf("leni0:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, \n",len,inpdata,chunked,cchunk, lc );
						inpchr=strchr(inpdata+cchunk,0x0D) ;
						if ((inpchr != NULL) &&(inpchr- (inpdata+cchunk) <16))
							*inpchr = 0; // replace lf by a end of string
						else {
/*							printf("0D not found\n");
							printf("len:%d, inpdata:%x, pdata:%x,chunked:%d  cchunk:%d, lc:%d, str:%s\n",len,inpdata,pdata,chunked,cchunk, lc,inpdata+cchunk );*/
							clientDisconnect("chunk"); clientConnect();
							lc = 0; 
							break;
						}
						chunked = (uint32_t) strtol(inpdata+cchunk, NULL, 16)+2;  // new chunk lenght including cr lf
						clen = strlen(inpdata+cchunk)  +2;
						lc = lc -cchunk  -clen; // rest after
//	printf("leni:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, clen:%d, str: %s\n",len,inpdata,chunked,cchunk, lc,clen,inpdata+cchunk );
						// compact data without chunklen and crlf
						if (cchunk >1){
							memcpy (inpdata+cchunk-2,pdata+len-lc, lc); 
//	printf("lenm:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d\n",len,inpdata,chunked,cchunk, lc);
							len -= (clen +2);
							inpdata +=   (cchunk -2);
//	printf("memcpy1 at %x from %x, lc:%d\n",inpdata+cchunk-2,pdata+len-lc,lc);
						}
						else{
							memcpy (inpdata,inpdata+cchunk+clen, lc); 
//	printf("lenm:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d\n",len,inpdata,chunked,cchunk, lc);
							len -= (clen + cchunk);							
//	printf("memcpy2 at %x from %x, lc:%d, len:%d\n",inpdata,inpdata+cchunk+clen,lc,len);
						}

						if (chunked > lc)
						{						
							cchunk = chunked - lc ;
							if (cchunk ==1) len --;
							if (cchunk ==0) len -=2;
							lc = 0;
						}	
						else
						{
							cchunk = chunked;
						}	
//	printf("leniout:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, metad:%d  clen:%d \n",len,inpdata,chunked,cchunk, lc,metad,clen );				
					}
				}
			}
		} 
		else 
		{
			if (chunked != 0) cchunk -= len; 
			lc = 0;
		}
		
// printf("CDATAOUT: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
		
// meta data computing
		if (rest <0) 
		{
			taskYIELD();
			ESP_LOGD(TAG,"clientReceiveCallback: pdata: %x, pdataend: %x, len: %d",(int)pdata,(int)pdata+len,len);
			ESP_LOGD(TAG,"Negative enter len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"",len,metad,rest,(int)pdata,pdata);
			if (len>-rest)
				*(pdata-rest) = 0; //truncated
			else
				*(pdata+len) = 0; //truncated
			strcat(metadata,pdata);
			if (len>-rest)
			{
				ESP_LOGD(TAG,"Negaposi   len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"",len,metad,rest,(int)pdata,pdata);
				clientSaveMetadata(metadata,strlen(metadata));
				metad = header.members.single.metaint ;
				pdata -= rest;	
				len += rest;
				rest = 0;
			}
			else
			{
				ESP_LOGD(TAG,"Negative   len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"",len,metad,rest,(int)pdata,pdata);
				pdata += len;
				rest += len;
				len = 0;
				ESP_LOGD(TAG,"Negatafter len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"",len,metad,rest,(int)pdata,pdata);
			}			
//printf("Negative len out = %d, pdata: %x,metad= %d  rest= %d \n",len,pdata,metad,rest);

		}
		inpdata = pdata;
		clen = len;
		if((header.members.single.metaint != 0)&&(clen > metad)) 
		{
//			ESP_LOGD(TAG,"clientReceiveCallback: pdata: %x, pdataend: %x, len: %d",(int)pdata,(int)pdata+len,len);

//			ESP_LOGD(TAG,"metain len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,(int)inpdata,rest );
			int jj = 0;
			while ((clen > metad)&&(header.members.single.metaint != 0)) // in buffer
			{
//				ESP_LOGD(TAG,"metainb len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,(int)inpdata,rest );
				jj++;
				l = inpdata[metad]*16;	//new meta length
				rest = clen - metad  -l -1;
				
//if (l ==0){
//	printf("mt len:%d, clen:%d, metad:%d,&l:%x, l:%d, rest:%d\n",len,clen,metad,inpdata+metad, l,rest );
//if (l > 80) dump(inpdata,len);
	
				if (l !=0)
				{
ESP_LOGD(TAG,"clientReceiveCallback: pdata: %x, pdataend: %x, len: %d",(int)pdata,(int)pdata+len,len);
ESP_LOGD(TAG,"mt len:%d, clen:%d, metad:%d ,&l:%x, l:%d, inpdata:%x, rest:%d, str: %s",len,clen,metad,(int)inpdata+metad, l,(int)inpdata,rest,inpdata+metad+1 );
					if (rest <0)
					{
						*(inpdata+clen) = 0; //truncated
ESP_LOGD(TAG,"mtlen len:%d, clen:%d, metad:%d, l:%d, inpdata:%x,  rest:%d",len,clen,metad, l,(int)inpdata,rest );				
						
						if (metadata != NULL) incfree(metadata,"meta"); 
						metadata = incmalloc(l+1);	
						strcpy(metadata,inpdata+metad+1);
					}
					else clientSaveMetadata(inpdata+metad+1,l);
				}	
				if (metad >0)
				{		
//					if (spiRamFifoFree() < metad) ESP_LOGV(TAG,"metaout wait metad: %d, bufferfree: %d",metad,spiRamFifoFree());			
					while(spiRamFifoFree()<metad)	 // wait some room
						vTaskDelay(30);
					audio_stream_consumer((char*)inpdata, metad, (void*)player_config);
				}
				metad  = header.members.single.metaint;
				inpdata = inpdata+clen-rest;
if (rest <0) ESP_LOGD(TAG,"mt1 len:%d, clen:%d, metad:%d, l:%d, inpdata:%x,  rest:%d",len,clen,metad, l,(int)inpdata,rest );
				clen = rest;				
				if (rest <0) 
				{
ESP_LOGD(TAG,"mt2 len:%d, clen:%d, metad:%d, l:%d, inpdata:%x,  rest:%d",len,clen,metad, l,(int)inpdata,rest );
					clen = 0; 
					break;
				}
			}	// while in buffer
			if (rest >=0)
			{	
				metad = header.members.single.metaint - rest ; //until next
				if (rest >0)
				{
//					if (spiRamFifoFree() < rest) ESP_LOGV(TAG,"metaout wait rest: %d, bufferfree: %d",rest,spiRamFifoFree());			
					while(spiRamFifoFree()<rest)	 // wait some room						
						vTaskDelay(30);// 
					audio_stream_consumer((char*)inpdata, rest, (void*)player_config);					
				}
				rest = 0;
			}	
//ESP_LOGD(TAG,"metaout len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d",len,clen,metad, l,(int)inpdata,rest );						
		} else 
		{		
	
			if (header.members.single.metaint != 0) metad -= len;
//printf("out len = %d, metad = %d  metaint= %d, rest:%d\n",len,metad,header.members.single.metaint,rest);
			if (len >0) 
			{
//				if (spiRamFifoFree() < len) ESP_LOGV(TAG,"metaout wait len: %d, bufferfree: %d",len,spiRamFifoFree());							
				while(spiRamFifoFree()<len)	 // wait some room	
						vTaskDelay(30); 
				audio_stream_consumer((char*)pdata+rest, len, (void*)player_config);
			}			
		}
// ---------------			
		if (!playing )
		{
			setVolumei(0);
			playing=1;
			if (once == 0)vTaskDelay(20);
			else vTaskDelay(1);
			setVolumei(getVolume());
			kprintf(CLIPLAY,0x0d,0x0a);
			if (!ledStatus) gpio_set_level(getLedGpio(),1);
		}
	}
}



static uint8_t bufrec[RECEIVE+20];
static char useragent[50];

void clientTask(void *pvParams) { 
	portBASE_TYPE uxHighWaterMark;
	struct timeval timeout; 
    timeout.tv_usec = 0;
	int sockfd;
	int bytes_read;
//	char *useragent;
	struct device_settings* device;
	struct sockaddr_in dest;
//	uint8_t *bufrec;
	
	vTaskDelay(300);	

//	bufrec = incmalloc(RECEIVE+10);
//	useragent = incmalloc(50);
	
	device = getDeviceSettings();
	if (device != NULL)
	{
		strcpy(useragent,device->ua);
		if (strlen(useragent) == 0) 
		{
			strcpy(useragent,"Karadio/1.5");
			strcpy(device->ua,useragent);
		}	
		free(device);
	}
	

	//	portBASE_TYPE uxHighWaterMark;
//	clearHeaders();

//	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//	printf("watermark webclient:%d  heap:%d\n",uxHighWaterMark,xPortGetFreeHeapSize( ));
	
	while(1) {
		xSemaphoreGive(sConnected);
		if(xSemaphoreTake(sConnect, portMAX_DELAY)) {

			//VS1053_HighPower();
			xSemaphoreTake(sDisconnect, 0);	
			sockfd = socket(AF_INET, SOCK_STREAM, 0);
			ESP_LOGI(TAG,"Webclient socket: %d, errno: %d", sockfd, errno);
			if(sockfd >= 0) ;//{printf("WebClient Socket created\n"); }
			else printf(strcWEBSOCKET,"create",errno);
			bzero(&dest, sizeof(dest));
			dest.sin_family = AF_INET;
			dest.sin_port = htons(clientPort);
			dest.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr*)(server -> h_addr_list[0])));
			bytes_read = 0;
			/*---Connect to server---*/
			if(connect(sockfd, (struct sockaddr*)&dest, sizeof(dest)) >= 0) 
			{
//				printf("WebClient Socket connected\n");
				memset(bufrec,0, RECEIVE+20);
				
				char *t0 = strstr(clientPath, ".m3u");
				if (t0 == NULL)  t0 = strstr(clientPath, ".pls");
				if (t0 == NULL)  t0 = strstr(clientPath, ".xspf");				
				if (t0 == NULL)  t0 = strstr(clientPath, ".m3u8");	
				if (t0 == NULL)  t0 = strstr(clientPath, ".asx");					
				if (t0 != NULL)  // a playlist asked
				{
				  cstatus = C_PLAYLIST;
//printf("sprint%d\n",6);				  
				  sprintf((char*)bufrec, "GET %s HTTP/1.0\r\nHOST: %s\r\n\r\n", clientPath,clientURL); //ask for the playlist
			    } 
				else 
				{
//					if ((strcmp(clientPath,"/") ==0)&&(cstatus != C_HEADER0)) clientSetPath("/;");
					if (strcmp(clientURL,"stream.pcradio.biz") ==0) strcpy(useragent,"pcradio");
//printf("sprint%d\n",7);					
					sprintf((char*)bufrec, "GET %s HTTP/1.1\r\nHost: %s\r\nicy-metadata: 1\r\nUser-Agent: %s\r\n\r\n", clientPath,clientURL,useragent); 
				}
//printf("st:%d, Client Sent:\n%s\n",cstatus,bufrec);
				xSemaphoreTake(sConnected, 0);
				send(sockfd, (char*)bufrec, strlen((char*)bufrec), 0);								
///// set timeout
				if (once == 0)
					timeout.tv_sec = 10; 
				else
					timeout.tv_sec = 3; 

				if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
					printf(strcSOCKET,"setsockopt",errno);
				
//////				
				do
				{
					bytes_read = recvfrom(sockfd, bufrec,RECEIVE, 0, NULL, NULL);	
					if ( bytes_read < 0 )						
					ESP_LOGE(TAG,"Client socket: %d  read: %d  errno:%d ",sockfd, bytes_read,errno);	
//if (bytes_read < 1000 )  
//printf("Rec:%d\n%s\n",bytes_read,bufrec);
					
					if ( bytes_read > 0 )
					{
							clientReceiveCallback(sockfd,(char*)bufrec, bytes_read);
					}	
					vTaskDelay(1);
					if(xSemaphoreTake(sDisconnect, 0)){ clearHeaders(); break;	}
				}
				while ( bytes_read > 0 );
			} else
			{
				printf(strcSOCKET,"connect", errno);
				clientSaveOneHeader("Invalid address",15,METANAME);	
				wsHeaders();
				vTaskDelay(1);
				clientDisconnect("Invalid"); 
				close(sockfd);
				continue;
			}	
			/*---Clean up---*/
			if (bytes_read <= 0 )  //nothing received or error or disconnected
			{				
					if ((playing)&&(once == 0))  // try restart
					{
						clientDisconnect("try restart"); 
						clientConnect();
						playing=1; // force
//						printf(CLIPLAY,0x0d,0x0a);
					}	
					else if ((!playing)&&(once == 1)){ // nothing played. Force the read of the buffer
						// some data not played						
						if ((!playing )&& (spiRamFifoFill())) {						
							playing=1;
							vTaskDelay(1);
							setVolumei(getVolume());
							kprintf(CLIPLAY,0x0d,0x0a);
							while (spiRamFifoFill()) vTaskDelay(100);							
							vTaskDelay(150);
							playing=0;
							clientDisconnect("data not played"); 
						}
					}						
						//						
					else if ((!playing)&&(once == 0)) {  // nothing received
							clientSaveOneHeader(notfound, 9,METANAME);
							wsHeaders();
							vTaskDelay(1);
							clientDisconnect("not found"); 
							
					}	
					else{  //playing & once=1 and no more received stream
						while (spiRamFifoFill()) vTaskDelay(100);
						vTaskDelay(200);
						clientDisconnect("once"); 						
					}					
			}//jpc
						
			if (playing)  // stop clean
			{		
				audio_player_stop(); 
				//if (get_audio_output_mode() == VS1053) spiRamFifoReset();
				player_config->media_stream->eof = true;
//				bufferReset();
				setVolumei(0);
				if (get_audio_output_mode() == VS1053)VS1053_flush_cancel(2);
				playing = 0;
				vTaskDelay(40);	// stop without click
				//VS1053_LowPower();
				setVolumei(getVolume());
			}	

//			bufferReset();
			shutdown(sockfd,SHUT_RDWR); // stop the socket
			vTaskDelay(1);	
			close(sockfd);
//			printf("WebClient Socket closed\n");
			if (cstatus == C_PLAYLIST) 			
			{
			  clientConnect();
			}
			uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
			ESP_LOGI(TAG,"watermark : %x  %d",uxHighWaterMark,uxHighWaterMark);	
		}
	}
}
