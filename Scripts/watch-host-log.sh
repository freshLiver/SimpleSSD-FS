#!/bin/bash
watch -n 10 'tail $(ls -1 logs/*.host.log | tail -1)'
