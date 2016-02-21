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

LAUNCH_FILE=launch.sh
LAUNCH_ARGS="-k 120"
TALLY_EXE=tally.sh
SCRIPT_PATH=`readlink -f $0`
SCRIPT_DIR=`dirname ${SCRIPT_PATH}`

cd "${SCRIPT_DIR}"
ALL_LAUNCH_ARGS=${LAUNCH_ARGS}

while true; do
	rm -f ${LAUNCH_FILE}
	./pinch ${ALL_LAUNCH_ARGS}
	status=$?
	if [ $status -eq 2 ]; then
		sudo shutdown -h now
		break
	elif [ $status -eq 1 ] || [ $status -eq 3 ]; then
		name=`head -1 "${LAUNCH_FILE}" | sed -E 's/# +//'`
		if [ $status -ne 3 ]; then
			if [ -f ${TALLY_EXE} ]; then
				./${TALLY_EXE} "${name}" &
			fi
		fi
		
		ALL_LAUNCH_ARGS=${LAUNCH_ARGS}
		sh ${LAUNCH_FILE}

		if [ $? -eq 2 ]; then #timed out
			ALL_LAUNCH_ARGS="${ALL_LAUNCH_ARGS} --launch-next"
		fi
		cd ${SCRIPT_DIR}
	else
		break
	fi
done

