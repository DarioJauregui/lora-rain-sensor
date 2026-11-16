#pragma once
#include <stdint.h>
#include <stdio.h>
#include <time.h>
namespace rainlog {
inline bool parseUtc(const char *s,uint64_t &ms){
 int y,m,d,h,n,sec,millis,end=0;
 if(sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d.%3dZ%n",&y,&m,&d,&h,&n,&sec,&millis,&end)!=7||end!=24||s[end]||y<1970||m<1||m>12||d<1||h<0||h>23||n<0||n>59||sec<0||sec>59||millis<0||millis>999)return false;
 const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 int limit=days[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0));if(d>limit)return false;
 y-=m<=2;int era=y/400,yoe=y-era*400;
 int doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 int64_t date=era*146097LL+yoe*365+yoe/4-yoe/100+doy-719468;
 ms=((date*24+h)*3600+n*60+sec)*1000+millis;return true;
}
inline void utc(uint64_t ms,char *out,size_t size){
 time_t sec=ms/1000;tm t{};gmtime_r(&sec,&t);
 snprintf(out,size,"%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec,unsigned(ms%1000));
}
}
