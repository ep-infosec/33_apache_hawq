#!/bin/bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

if [ x$1 != x ] ; then
    GPHOME_PATH=$1
else
    GPHOME_PATH="\`pwd\`"
fi

if [ "$2" = "ISO" ] ; then
	cat <<-EOF
		if [ "\${BASH_SOURCE:0:1}" == "/" ]
		then
		    GPHOME=\`dirname "\$BASH_SOURCE"\`
		else
		    GPHOME=\`pwd\`/\`dirname "\$BASH_SOURCE"\`
		fi
	EOF
else
	cat <<-EOF
		GPHOME=${GPHOME_PATH}
	EOF
fi


PLAT=`uname -s`
if [ $? -ne 0 ] ; then
    echo "Error executing uname -s"
    exit 1
fi

cat << EOF

# Replace with symlink path if it is present and correct
if [ -h \${GPHOME}/../hawq ]; then
    GPHOME_BY_SYMLINK=\`(cd \${GPHOME}/../hawq/ && pwd -P)\`
    if [ x"\${GPHOME_BY_SYMLINK}" = x"\${GPHOME}" ]; then
        GPHOME=\`(cd \${GPHOME}/../hawq/ && pwd -L)\`/.
    fi
    unset GPHOME_BY_SYMLINK
fi
EOF

cat <<EOF
PATH=\$GPHOME/bin:\$JAVA_HOME/bin:\$PATH
EOF

PERL_VERSION=`perl -v 2>/dev/null | sed -n 's/This is perl.*v[a-z ]*\([0-9]\.[0-9][0-9.]*\).*$/\1/p'`

if [ "${PLAT}" = "Darwin" ] ; then
	cat <<EOF
DYLD_LIBRARY_PATH=\$GPHOME/lib:\$JAVA_HOME/jre/lib/amd64/server:\$DYLD_LIBRARY_PATH
EOF
else
    cat <<EOF
LD_LIBRARY_PATH=\$GPHOME/lib:\$GPHOME/lib/perl5/${PERL_VERSION}/x86_64-linux-thread-multi/CORE:\$JAVA_HOME/jre/lib/amd64/server:\$LD_LIBRARY_PATH
EOF
fi

# leverage dependency libraries

cat <<EOF
DEPENDENCY_PATH=$DEPENDENCY_PATH
EOF

if [ "${PLAT}" != "Darwin" ] ; then
    cat <<EOF
LD_LIBRARY_PATH=\$DEPENDENCY_PATH/lib:\$LD_LIBRARY_PATH:/usr/local/lib
EOF
    else
    cat <<EOF
DYLD_LIBRARY_PATH=\$DEPENDENCY_PATH/lib:\$DYLD_LIBRARY_PATH
EOF
fi

#setup PERLPATH
if [ "${PLAT}" != "Darwin" ] ; then
    cat <<EOF
PERL5LIB=\$GPHOME/lib/perl5/${PERL_VERSION}:\$GPHOME/lib/perl5/site_perl/${PERL_VERSION}
EOF
fi

#setup PYTHONPATH
cat <<EOF
PYTHONPATH=\$GPHOME/lib/python:\$PYTHONPATH
EOF

# openssl configuration file path
cat <<EOF
OPENSSL_CONF=\$GPHOME/etc/openssl.cnf
EOF

# libhdfs3 configuration file path
cat << EOF
LIBHDFS3_CONF=\$GPHOME/etc/hdfs-client.xml
EOF

# libyarn configuration file path
cat << EOF
LIBYARN_CONF=\$GPHOME/etc/yarn-client.xml
EOF

# global resource manager configuration file path
cat << EOF
HAWQSITE_CONF=\$GPHOME/etc/hawq-site.xml
EOF

cat <<EOF
export GPHOME
export PATH
export DEPENDENCY_PATH
export PERL5LIB
EOF

if [ "${PLAT}" != "Darwin" ] ; then
cat <<EOF
export LD_LIBRARY_PATH
EOF
else
cat <<EOF
export DYLD_LIBRARY_PATH
EOF
fi

cat <<EOF
export PYTHONPATH
EOF

cat <<EOF
export OPENSSL_CONF
EOF

cat <<EOF
export LIBHDFS3_CONF
EOF

cat <<EOF
export LIBYARN_CONF
EOF

cat <<EOF
export HAWQSITE_CONF
EOF
