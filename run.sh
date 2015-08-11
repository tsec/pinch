#! /bin/bash
##
## Copyright (C) 2015 Akop Karapetyan
##
## Licensed under the Apache License, Version 2.0 (the "License");
## you may not use this file except in compliance with the License.
## You may obtain a copy of the License at
##
## http://www.apache.org/licenses/LICENSE-2.0
##
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##

LAUNCH=launch.sh

full_path=`readlink -f $0`
dir=`dirname $full_path`

cd $dir

while true; do
	rm -f $LAUNCH
	./pinch
	status=$?
	if [ $status -eq 2 ]; then
		sudo shutdown -h now
		break
	elif [ $status -eq 1 ]; then
		sh $LAUNCH
	else
		break
	fi
done

