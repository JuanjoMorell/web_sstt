#!/bin/bash

echo "--Pasando archivo al servidor--"

scp $1 alumno@192.168.56.104:/home/alumno

echo "--Transferencia hecha--"
