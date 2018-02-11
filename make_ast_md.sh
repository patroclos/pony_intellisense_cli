#!/bin/bash
# creates a .md file with source and ast of $1 and writes it to $2

content=`cat $1`
ast=`echo $content | cmake-build-debug/ponylang_llvm_tester --stdin dump-ast`

echo -e "Source\n\n\`\`\`pony" > $2
echo "$content" >> $2
echo -e "\`\`\`\n\n" >> $2
echo -e "AST\n\n\`\`\`" >> $2
echo "$ast" >> $2
echo -e "\`\`\`\n" >> $2
