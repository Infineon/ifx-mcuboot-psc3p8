#!/usr/bin/env python
# Clean repo-staging and non-public files
# Checks submodules and copyrights

import logging
from os import remove, system
from shutil import rmtree
from sys import argv, exit

def check_submodules():
    with open('../../../.gitmodules', 'r') as file:
        lines = file.readlines()
    correct_submodules = True
    for line in lines:
        if line.strip().startswith('url =') and line.strip()[:25] != 'url = https://github.com/':
            logging.error(f".gitmodules file have non-github submodule: {line.strip()}")
            correct_submodules = False
    if not correct_submodules:
        exit(-1)
    logging.info("All submodules in .gitmodules file are correct")

def remove_non_public_files():
    # Remove staging files by default script
    system('python ./repo-staging-cleaner.py')

    # Script self destruction
    remove(argv[0])
    logging.info('Cypress release cleanup complete')


logging.basicConfig(level = logging.DEBUG, format = '%(levelname)s: %(message)s')
check_submodules()
remove_non_public_files()
