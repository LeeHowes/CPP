#!/bin/bash
curl https://api.csswg.org/bikeshed/ -F file=@$1.bs -F force=1 > $1.html
