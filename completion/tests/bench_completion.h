/* This file is part of KDevelop
    Copyright 2010 Milian Wolff <mail@milianw.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef PHP_BENCHMARKCODECOMPLETION_H
#define PHP_BENCHMARKCODECOMPLETION_H

#include "duchain/tests/duchaintestbase.h"

namespace Php {

class BenchmarkCodeCompletion : public DUChainTestBase
{
    Q_OBJECT

public slots:
    // don't do the stuff we do in the DUChainTestBase
    void initTestCase();
    // shutdown in a proper way, the DUChain we create here can be stored
    void cleanupTestCase();

private slots:
    void globalCompletion();
    void completionData();
};

}

#endif // PHP_BENCHMARKCODECOMPLETION_H
