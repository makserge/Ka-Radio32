#ifndef ADDONUCG_H_
#define ADDONUCG_H_
void playingUcg();
void namesetUcg(char* ici);
void statusUcg(char* label);
void icy0Ucg(char* ici);
void icy4Ucg(char* ici);
void metaUcg(char* ici);
char* getNameNumUcg();
void scrollUcg();
void drawFrameUcg(uint8_t mTscreen,struct tm *dt);
void drawTTitleUcg(char* ttitle);
void drawNumberUcg(uint8_t mTscreen,char* irStr);
void drawStationUcg(uint8_t mTscreen,char* snum,char* ddot);
void drawVolumeUcg(uint8_t mTscreen,char* aVolume);
void drawTimeUcg(uint8_t mTscreen,struct tm *dt,unsigned timein);
void lcd_initUcg(uint8_t* lcd_type);
void setVolumeUcg(uint16_t vol);
void drawLinesUcg();
ucg_int_t ucgDrawString(ucg_t *ucg, ucg_int_t x, ucg_int_t y, uint8_t dir, const char *str);


#endif /* ADDONUCG_H_ */
