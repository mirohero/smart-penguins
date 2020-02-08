#!/bin/bash
FULL_PATH="$PWD"
NODE_ID=0
INCREMENT=0
FLASH_FAILED=0

function main {
    init_inputs

    procedure
    while true; do
        echo "[y] Continue with next nodeId: $(($NODE_ID+$INCREMENT))?"
        echo "[n] Exit?"
        read -p "> " yn
        case $yn in
            [Yy]* ) fn_cd $FULL_PATH;
                    echo $PWD
                    NODE_ID=$(($NODE_ID+$INCREMENT));
                    procedure;;
            [Nn]* ) exit;;
            * ) echo "Please answer yes or no.";;
        esac
    done
}

function fn_cd {
    cd $1
}

function validate {
    if ! [[ "$1" =~ ^[0-9]+$ ]]
        then
            echo "Integers only..."
            exit 1
    fi
}

function init_inputs {
    read -p "Set initial nodeId: " NODE_ID
    validate $NODE_ID

    read -p "Set increment steps for nodeId: " INCREMENT
    validate $INCREMENT

    echo
}

function procedure {
    edit_configs
    build
    flash
    while [ $FLASH_FAILED -gt 0 ]; do
        echo "[y/n] Retry flashing device with nodeId: $NODE_ID?"
        read -p "> " yn
        case $yn in
            [Yy]* ) flash;;
            [Nn]* ) break;;
            * ) echo "Please answer yes or no.";;
        esac
    done
}

function edit_configs {
    fn_cd "../config/featuresets"
    echo $PWD

    #Get line number where 'nodeId' is set
    LINE_NUM=$(echo | grep -n 'c->nodeId' github.cpp | cut -d : -f 1)
    #Get string in line number
    LINE=$(echo | sed "${LINE_NUM}q;d" github.cpp)
    #Extract old nodeId value
    OLD_NODE_ID=$(echo $LINE | sed 's/[^0-9]*//g')
    #Replace old nodeId with new nodeId
    sed -i "s/c->nodeId = $OLD_NODE_ID/c->nodeId = $NODE_ID/g" github.cpp
}

function build {
    #Build
    fn_cd "../../"
    echo $PWD
    make ENV=docker
}

function flash {
    #Flash
    if [ $FLASH_FAILED -eq 0 ]; then
        fn_cd "_build/release/NRF52/github/"
        echo $PWD
    fi
    nrfjprog --program FruityMesh.hex --sectorerase -r
    FLASH_FAILED=$?
    echo
}

main
