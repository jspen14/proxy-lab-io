#!/bin/bash

for ((i=1;i<=10;i++));
do
  
	curl -s -o file-no$i.txt "http://localhost:45806/cgi-bin/slow?1&4096&$i" &
	curl -s -o file-$i.txt --proxy http://localhost:45809 "http://localhost:45806/cgi-bin/slow?1&4096&$i" &
	
done

wait

for ((i=1;i<=10;i++));
do
  
	diff -u file-no$i.txt file-$i.txt
	
done

  
