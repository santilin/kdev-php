/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>     *
 *   Copyright 2008 Niko Sams <niko.sams@gmail.com>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/
#include "expressionparser.h"
#include "parsesession.h"
#include "phpast.h"
#include "phpparser.h"
#include "phpdebugvisitor.h"
#include "expressionvisitor.h"

#include <language/duchain/ducontext.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchain.h>

#include <kdebug.h>
using namespace KDevelop;

namespace Php {

ExpressionParser::ExpressionParser( bool strict, bool debug ) : m_strict(strict), m_debug(debug)
{
}

ExpressionEvaluationResult ExpressionParser::evaluateType( const QByteArray& expression, DUContextPointer context, const KDevelop::TopDUContext* source)
{
    if( m_debug )
        kDebug() << "==== .Evaluating ..:" << endl << expression;

    ParseSession* session = new ParseSession();
    session->setContents(expression);
    Parser* parser = session->createParser(Parser::DefaultState);
    ExprAst* ast = 0;
    if (!parser->parseExpr(&ast)) {
        kDebug() << "Failed to parse \"" << expression << "\"";
        delete session;
        delete parser;
        return ExpressionEvaluationResult();
    }
    ast->ducontext = dynamic_cast<DUContext*>(context.data());

    ExpressionEvaluationResult ret = evaluateType( ast, session, source );
    delete session;
    delete parser;

    return ret;
}

ExpressionEvaluationResult ExpressionParser::evaluateType( AstNode* ast, ParseSession* session, const KDevelop::TopDUContext* source)
{
    if (m_debug) {
        kDebug() << "===== AST:";
        DebugVisitor debugVisitor(session->tokenStream(), session->contents());
        debugVisitor.visitNode(ast);
    }

    ExpressionVisitor v(session, source, m_strict);
    v.visitNode( ast );	

    return v.result();
}

}
