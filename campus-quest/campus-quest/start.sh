#!/bin/bash
cd "$(dirname "$0")" || exit 1
node -e "if (Number(process.versions.node.split('.')[0]) < 22 || (Number(process.versions.node.split('.')[0]) === 22 && Number(process.versions.node.split('.')[1]) < 13)) { console.error('Campus Quest requires Node.js 22.13 or newer.'); process.exit(1); }" || exit 1
node server.js
