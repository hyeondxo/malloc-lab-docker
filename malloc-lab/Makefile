#
# Students' Makefile for the Malloc Lab
#
TEAM = bovik
VERSION = 1
HANDINDIR = /afs/cs.cmu.edu/academic/class/15213-f01/malloclab/handin

CC = gcc
# CFLAGS = -Wall -O2 -m32
CFLAGS = -Wall -O2 -g

OBJS_COMMON = mdriver.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

# 기본 타겟 : 두 실행파일 모두 빌드
all: mdriver_mm mdriver_ex

# 묵시적 가용 리스트 방식(mm.c)
mdriver_mm: $(OBJS_COMMON) mm.O2
	$(CC) $(CFLAGS) -o $@ $^

# 명시적 가용 리스트 방식(mm_ex.c)
mdriver_ex: $(OBJS_COMMON) mm_ex.o
	$(CC) $(CFLAGS) -o $@ $^

# 개별 오브젝트 규칙
mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

mm.o: mm.c mm.h memlib.h
	$(CC) $(CFLAGS) -c mm.c -o $@

mm_ex.o: mm_ex.c mm.h memlib.h
	$(CC) $(CFLAGS) -c mm_ex.c -o $@

handin:
	cp mm.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm.c

handin_ex:
	cp mm_ex.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm_ex.c

clean:
	rm -f *~ *.o mdriver mdriver_mm mdriver_ex


