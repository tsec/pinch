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

EMU_DIR=../fba-pi

EMU_EXE=fbapi
EMU_ARGS="-f -k 120"
LAUNCH_ARGS="-k 120"
TALLY_EXE=tally.sh
LAUNCH=launch.name
SCRIPT_PATH=`readlink -f $0`
SCRIPT_DIR=`dirname ${SCRIPT_PATH}`

cd "${SCRIPT_DIR}"
ALL_LAUNCH_ARGS=${LAUNCH_ARGS}

while true; do
	rm -f ${LAUNCH}
	./pinch ${ALL_LAUNCH_ARGS}
	status=$?
	if [ $status -eq 2 ]; then
		sudo shutdown -h now
		break
	elif [ $status -eq 1 ] || [ $status -eq 3 ]; then
		name=`cat ${LAUNCH}`

		if [ $status -ne 3 ]; then
			if [ -f ${TALLY_EXE} ]; then
				./${TALLY_EXE} "${name}" &
			fi
		fi

		ALL_LAUNCH_ARGS=${LAUNCH_ARGS}

		cd ${EMU_DIR}
		./${EMU_EXE} ${EMU_ARGS} "${name}"
		if [ $? -eq 2 ]; then #timed out
			ALL_LAUNCH_ARGS="${ALL_LAUNCH_ARGS} --launch-next"
		fi
		cd ${SCRIPT_DIR}
	else
		break
	fi
done

