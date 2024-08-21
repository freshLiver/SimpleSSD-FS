#!/bin/bash
watch -n 1 'tail $(ls -1 logs/*.host.log | tail -1)'
