#!/bin/bash

echo "--Subiendo datos a Github--"

git add .
git commit -m "$1"
git push origin master

echo "--Transferencia completada--"
