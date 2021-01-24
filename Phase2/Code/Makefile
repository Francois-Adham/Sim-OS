build:
	gcc process_generator.c -o process_generator.out -lm
	gcc clk.c -o clk.out -lm
	gcc scheduler.c -o scheduler.out -lm
	gcc process.c -o process.out -lm
	gcc test_generator.c -o test_generator.out -lm

clean:
	rm -f *.out *.log *.perf processes.txt

all: clean build kill

run:
	./process_generator.out

kill:
	ipcrm -a
	killall -e clk.out
	killall -e scheduler.out 
	killall -e process_generator.out
	killall -e process.out
	killall -e make
