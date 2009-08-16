/*
   Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>
   Copyright 2008 Hamish Rodda <rodda@kde.org>
   Copyright 2008 Niko Sams <niko.sams@gmail.com>
   Copyright 2009 Milian Wolff <mail@milianw.de>

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

#include "context.h"

#include "duchain/expressionparser.h"
#include "helpers.h"

#include "../duchain/helper.h"
#include "../duchain/variabledeclaration.h"

#include "implementationitem.h"
#include "keyworditem.h"

#include "../parser/generated/phpparser.h"
#include "../parser/tokentext.h"

#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <klocalizedstring.h>
#include <language/duchain/ducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/stringhelpers.h>
#include <language/duchain/types/identifiedtype.h>
#include <language/duchain/types/structuretype.h>
#include <language/interfaces/iproblem.h>
#include <util/pushvalue.h>
#include <language/duchain/codemodel.h>
#include <language/duchain/classdeclaration.h>

#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <project/projectmodel.h>
#include <language/duchain/types/unsuretype.h>
#include <language/duchain/parsingenvironment.h>

#include <language/util/includeitem.h>
#include "includefileitem.h"

#define LOCKDUCHAIN     DUChainReadLocker lock(DUChain::lock())

#define ifDebug(x) x
#include <language/codecompletion/codecompletion.h>

using namespace KDevelop;

namespace Php
{

typedef QList<Parser::TokenType> TokenList;

/**
 * Utility class which makes it easier to access the relevant parts
 * of the token stream for code completion.
 *
 * TODO: This class should be reviewed imo - I just hacked it together, quick'n'dirty
 */
class TokenAccess {
public:
    /// Setup the token stream from the input code
    TokenAccess(CodeCompletionContext* context)
        : m_context(context)
    {
        Q_ASSERT(m_context);

        Lexer lexer(&m_stream, m_context->code());
        int token;
        while ((token = lexer.nextTokenKind())) {
            Parser::Token &t = m_stream.next();
            t.begin = lexer.tokenBegin();
            t.end = lexer.tokenEnd();
            t.kind = token;
        }
        // move to last token
        m_pos = m_stream.size() - 1;
    }

    /// returns Token_INVALID if the position is invalid
    /// else returns the type of the current token
    int type() const {
        if ( m_pos == -1 ) {
            return Parser::Token_INVALID;
        } else {
            return m_stream.token(m_pos).kind;
        }
    }

    /// convenience comparison to a tokentype
    bool operator==(const Parser::TokenType& other) const {
        return other == type();
    }

    /// move to previous token
    void pop() {
        if ( m_pos >= 0 ) {
            --m_pos;
        }
    }

    /// move relative to current token
    /// NOTE: make sure you honor the boundaries.
    void moveTo(const qint64 &relPos) {
        m_pos += relPos;
        Q_ASSERT(m_pos > 0);
        Q_ASSERT(m_pos < m_stream.size());
    }

    /// get type of token relative to current position
    /// returns Token_INVALID if the position goes out of the boundaries
    int typeAt(const qint64 &relPos) const {
        const qint64 pos = m_pos + relPos;
        if ( pos >= 0 && pos < m_stream.size() ) {
            return m_stream.token(pos).kind;
        } else {
            return Parser::Token_INVALID;
        }
    }

    /// Get string for token at a given position relative to the current one.
    /// NOTE: Make sure you honor the boundaries.
    QString stringAt(const qint64 &relPos) const {
        Parser::Token token = tokenAt(relPos);
        return m_context->code().mid(token.begin, token.end - token.begin + 1);
    }

    /// check whether the current token is prepended by the list of tokens
    bool prependedBy(const TokenList &list) const {
        // this would be useless, hence forbid it
        Q_ASSERT ( !list.isEmpty() );

        if ( m_pos < list.count() - 1 ) {
            // not enough tokens
            return false;
        } else {
            uint pos = 1;
            foreach ( const Parser::TokenType& type, list ) {
                if ( m_stream.token( m_pos - pos ).kind == type ) {
                    ++pos;
                    continue;
                } else {
                    return false;
                }
            }
            return true;
        }
    }

    /// Get the token relative to the current one.
    /// NOTE: Make sure you honor the boundaries.
    Parser::Token tokenAt(const qint64 &relPos) const {
        const qint64 pos = m_pos + relPos;
        Q_ASSERT(pos >= 0);
        Q_ASSERT(pos < m_stream.size());
        return m_stream.token(pos);
    }

private:
    CodeCompletionContext* m_context;
    TokenStream m_stream;
    qint64 m_pos;
};

/// add keyword to list of completion items
#define ADD_KEYWORD(x) items << CompletionTreeItemPointer( new KeywordItem( x, KDevelop::CodeCompletionContext::Ptr(this) ) );

int completionRecursionDepth = 0;

CodeCompletionContext::CodeCompletionContext(KDevelop::DUContextPointer context, const QString& text, const QString& followingText, const KDevelop::SimpleCursor& position, int depth)
        : KDevelop::CodeCompletionContext(context, text, position, depth)
        , m_memberAccessOperation(NoMemberAccess), m_parentAccess(false)
{
    m_valid = isValidPosition();
    if (!m_valid) {
        log("position not valid for code-completion");
        return;
    }

    ifDebug(log("non-processed text: " + m_text);)

    if ( context->type() == DUContext::Class || context->type() == DUContext::Function || context->type() == DUContext::Other ) {
        ifDebug(log("added start tag: " + m_text);)
        m_text.prepend("<?php ");
    }

    TokenAccess lastToken(this);

    /// even when we skip to some more meaning ful token, this will
    /// always be the end position of the last token
    const qint64 lastTokenEnd = lastToken.tokenAt(0).end + 1;

    bool lastWasWhitespace = lastToken == Parser::Token_WHITESPACE;
    if ( lastWasWhitespace ) {
        ifDebug(log("skipping whitespace token");)
        lastToken.pop();
    }

    ifDebug(log(tokenText(lastToken.type()));)

    ///TODO: REFACTOR: push some stuff into its own methods
    ///                and call them from inside the big switch.
    ///                Then we can forget about having additional checks
    ///                beforehand and can handle it all in one place.

    // The following tokens require a whitespace after them for code-completion:
    if ( !lastWasWhitespace ) {
        switch ( lastToken.type() ) {
            case Parser::Token_EXTENDS:
            case Parser::Token_IMPLEMENTS:
            case Parser::Token_NEW:
            case Parser::Token_THROW:
                ifDebug(log("need whitespace after token for completion");)
                m_valid = false;
                return;
            default:
                break;
        }
    }

    if ( lastToken.type() == Parser::Token_LPAREN ) {
        qint64 pos = -1;
        if ( lastToken.typeAt(pos) == Parser::Token_WHITESPACE ) {
            --pos;
        }
        switch ( lastToken.typeAt(pos) ) {
            case Parser::Token_REQUIRE:
            case Parser::Token_REQUIRE_ONCE:
            case Parser::Token_INCLUDE:
            case Parser::Token_INCLUDE_ONCE:
                // we want file completion here (see below)
                lastToken.moveTo(pos);
                break;
            default:
                // nothing
                break;
        }
    }

    ifDebug(log(tokenText(lastToken.type()));)

    switch ( lastToken.type() ) {
        ///TODO: sometime in the future we should do skeleton-completion, e.g. after T_IF create something like
        ///      if (CURSOR) {...}, properly indented to the users style
        case Parser::Token_IF:
        case Parser::Token_WHILE:
        case Parser::Token_ELSE:
        case Parser::Token_ELSEIF:
        case Parser::Token_FOREACH:
        case Parser::Token_FOR:
        case Parser::Token_DO:
        case Parser::Token_ABSTRACT:
        case Parser::Token_BREAK:
        case Parser::Token_CLOSE_TAG:
        case Parser::Token_RBRACKET:
        case Parser::Token_RPAREN:
        case Parser::Token_VAR:
        case Parser::Token_CONST:
        case Parser::Token_INLINE_HTML:
        case Parser::Token_INVALID:
        case Parser::Token_FUNCTION:
        case Parser::Token_ARRAY:
            ifDebug(log("no completion after this token");)
            m_valid = false;
            break;
        case Parser::Token_EXTENDS:
            if ( lastToken.prependedBy(TokenList() << Parser::Token_WHITESPACE << Parser::Token_STRING
                                                   << Parser::Token_WHITESPACE << Parser::Token_CLASS) ) {
                m_memberAccessOperation = ClassExtendsChoose;
                forbidIdentifier(lastToken.stringAt(-2));
            } else if ( lastToken.prependedBy(TokenList() << Parser::Token_WHITESPACE << Parser::Token_STRING
                                                   << Parser::Token_WHITESPACE << Parser::Token_INTERFACE) ) {
                m_memberAccessOperation = InterfaceChoose;
                forbidIdentifier(lastToken.stringAt(-2));
            } else {
                ifDebug(log("token prepended by bad tokens, don't do completion");)
                m_valid = false;
            }
            break;
        case Parser::Token_IMPLEMENTS:
            if ( lastToken.prependedBy(TokenList() << Parser::Token_WHITESPACE << Parser::Token_STRING
                                                   << Parser::Token_WHITESPACE << Parser::Token_CLASS) ) {
                m_memberAccessOperation = InterfaceChoose;
                forbidIdentifier(lastToken.stringAt(-2));
            } else {
                ifDebug(log("token prepended by bad tokens, don't do completion");)
                m_valid = false;
            }
            break;
        case Parser::Token_COMMA:
            {
            // check if we are in the list after Token_IMPLEMENTS:
            qint64 relPos = -1;
            QList<qint64> identifierPositions;
            while ( true ) {
                if ( lastToken.typeAt(relPos) == Parser::Token_WHITESPACE ) {
                    --relPos;
                    continue;
                } else if ( lastToken.typeAt(relPos) == Parser::Token_STRING ) {
                    identifierPositions << relPos;
                    --relPos;
                    if ( lastToken.typeAt(relPos) == Parser::Token_WHITESPACE ) {
                        --relPos;
                    }
                            // interfaces may extend more than one interface
                    if ( ( lastToken.typeAt(relPos) == Parser::Token_EXTENDS &&
                            lastToken.typeAt(relPos - 1) == Parser::Token_WHITESPACE &&
                            lastToken.typeAt(relPos - 2) == Parser::Token_STRING &&
                            lastToken.typeAt(relPos - 3) == Parser::Token_WHITESPACE &&
                            lastToken.typeAt(relPos - 4) == Parser::Token_INTERFACE )
                        || // classes may implement more than one interface
                         ( lastToken.typeAt(relPos) == Parser::Token_IMPLEMENTS &&
                            lastToken.typeAt(relPos - 1) == Parser::Token_WHITESPACE &&
                            lastToken.typeAt(relPos - 2) == Parser::Token_STRING &&
                            lastToken.typeAt(relPos - 3) == Parser::Token_WHITESPACE &&
                            lastToken.typeAt(relPos - 4) == Parser::Token_CLASS ) )
                    {
                        identifierPositions << (relPos - 2);
                        m_memberAccessOperation = InterfaceChoose;
                        break;
                    } else if ( lastToken.typeAt(relPos) == Parser::Token_COMMA ) {
                        // skip to next entry
                        --relPos;
                        continue;
                    }
                } else {
                    break;
                }
            }
            if ( m_memberAccessOperation == InterfaceChoose ) {
                ifDebug(log("in implementation list");)
                m_memberAccessOperation = InterfaceChoose;
                foreach ( const qint64& pos, identifierPositions ) {
                    forbidIdentifier(lastToken.stringAt(pos));
                }
            } else {
                // else do function call completion
                m_memberAccessOperation = FunctionCallAccess;
            }
            }
            break;
        case Parser::Token_STRING:
            // don't do completion if no whitespace is given or
            if ( !lastWasWhitespace && lastToken.typeAt(-1) == Parser::Token_OPEN_TAG ) {
                ifDebug(log("no completion because string is prepended by <?");)
                m_valid = false;
            } else {
                // else just do normal completion
                m_memberAccessOperation = NoMemberAccess;
            }
            break;
        case Parser::Token_OBJECT_OPERATOR:
            m_memberAccessOperation = MemberAccess;
            lastToken.pop();
            break;
        case Parser::Token_PAAMAYIM_NEKUDOTAYIM:
            m_memberAccessOperation = StaticMemberAccess;
            lastToken.pop();
            break;
        case Parser::Token_LPAREN:
            {
            qint64 pos = -1;
            if ( lastToken.typeAt(pos) == Parser::Token_WHITESPACE ) {
                --pos;
            }
            if ( lastToken.typeAt(pos) == Parser::Token_CATCH ) {
                m_memberAccessOperation = ExceptionChoose;
            } else if ( lastToken.typeAt(pos) == Parser::Token_ARRAY ) {
                m_memberAccessOperation = NoMemberAccess;
                ifDebug(log("NoMemberAccess");)
                ifDebug(log("returning early");)
                return;
            } else {
                if (depth == 0) {
                    //The first context should never be a function-call context, so make this a NoMemberAccess context and the parent a function-call context.
                    m_parentContext = new CodeCompletionContext(m_duContext, m_text, QString(), m_position, depth + 1);
                    ifDebug(log("NoMemberAccess (created parentContext)");)
                    return;
                }
                m_memberAccessOperation = FunctionCallAccess;
                lastToken.pop();
            }
            }
            break;
        case Parser::Token_NEW:
            if ( lastToken.prependedBy(TokenList() << Parser::Token_WHITESPACE << Parser::Token_THROW) ) {
                m_memberAccessOperation = ExceptionChoose;
            } else {
                m_memberAccessOperation = NewClassChoose;
            }
            break;
        case Parser::Token_THROW:
            m_memberAccessOperation = ExceptionInstanceChoose;
            break;
        case Parser::Token_REQUIRE:
        case Parser::Token_REQUIRE_ONCE:
        case Parser::Token_INCLUDE:
        case Parser::Token_INCLUDE_ONCE:
            // check if we can do file completion
            {
                const QString existingText = m_text.mid( lastTokenEnd ).append(followingText).trimmed();

                if ( existingText.startsWith('"') || existingText.startsWith('\'') ) {
                    m_memberAccessOperation = FileChoose;
                    m_expression = existingText.mid(1);
                } else {
                    m_valid = false;
                }
            }
            break;
        default:
            // normal completion is valid
            if ( duContext() && duContext()->type() == DUContext::Class ) {
                // when we are inside a class context, give overloadable members as completion
                m_memberAccessOperation = ClassMemberChoose;
            } else {
                m_memberAccessOperation = NoMemberAccess;
            }
            break;
    }

    ifDebug(
        switch ( m_memberAccessOperation ) {
            case FileChoose:
                log("FileChoose");
                break;
            case ExceptionInstanceChoose:
                log("ExceptionInstanceChoose");
                break;
            case ExceptionChoose:
                log("ExceptionChoose");
                break;
            case ClassMemberChoose:
                log("ClassMemberChoose");
                break;
            case NoMemberAccess:
                log("NoMemberAccess");
                break;
            case NewClassChoose:
                log("NewClassChoose");
                break;
            case FunctionCallAccess:
                log("FunctionCallAccess");
                break;
            case InterfaceChoose:
                log("InterfaceChoose");
                break;
            case ClassExtendsChoose:
                log("ClassExtendsChoose");
                break;
            case MemberAccess:
                log("MemberAccess");
                break;
            case StaticMemberAccess:
                log("StaticMemberAccess");
                break;
        }
    )

    // if it's not valid, we should return early
    if ( !m_valid ) {
        ifDebug(log("invalid completion");)
        return;
    }

    // check whether we need the expression or have everything we need and can return early
    switch ( m_memberAccessOperation ) {
        // these access operations don't need the previous expression evaluated
        case FileChoose:
        case ClassMemberChoose:
        case InterfaceChoose:
        case NewClassChoose:
        case ExceptionChoose:
        case ExceptionInstanceChoose:
        case ClassExtendsChoose:
        case NoMemberAccess:
            ifDebug(log("returning early");)
            return;
        case FunctionCallAccess:
        case MemberAccess:
        case StaticMemberAccess:
            // these types need the expression evaluated
            break;
    }

    // Now find out where the expression starts

    ifDebug(log(tokenText(lastToken.type()));)

    m_text = m_text.left(lastToken.tokenAt(0).end + 1).trimmed();
    ifDebug(log(m_text);)

    ///TODO: use the token stream here as well
    int start_expr = expressionAt(m_text, m_text.length());

    m_expression = m_text.mid(start_expr).trimmed();

    QString expressionPrefix = stripFinalWhitespace(m_text.left(start_expr));
    ifDebug(log("expressionPrefix: " + expressionPrefix);)

    /**
     * Possible cases:
     * a = exp; - partially handled
     * ...
     * return exp;
     * throw exp;
     * new Class;
     * a=function(exp
     * a = exp(
     * ClassType instance(
     *
     * What else?
     *
     * When the left and right part are only separated by a whitespace,
     * expressionAt returns both sides
     * */

    ///Handle recursive contexts(Example: "ret = function1(param1, function2(" )
    if (expressionPrefix.endsWith('(') || expressionPrefix.endsWith(',')) {
        log(QString("Recursive function-call: Searching parent-context in \"%1\"").arg(expressionPrefix));
        //Our expression is within a function-call. We need to find out the possible argument-types we need to match, and show an argument-hint.

        //Find out which argument-number this expression is, and compute the beginning of the parent function-call(parentContextLast)
        QStringList otherArguments;
        int parentContextEnd = expressionPrefix.length();

        skipFunctionArguments(expressionPrefix, otherArguments, parentContextEnd);

        QString parentContextText = expressionPrefix.left(parentContextEnd);

        if (parentContextText == text) {
            log(QString("Endless recursion spotted, this doesn't seem to be a valid position for a code completion."));
            m_valid = false;
            return;
        }

        log(QString("This argument-number: %1 Building parent-context from \"%2\"").arg(otherArguments.size()).arg(parentContextText));

        m_parentContext = new CodeCompletionContext(m_duContext, parentContextText, QString(), m_position, depth + 1);

        if (!m_parentContext->isValid()) {
            m_parentContext = 0;
            m_valid = false;
            return;
        }
    }

    // Now care about m_expression

    /// When isReturn is true, we should match the result against the return-type of the current context-function
    bool isReturn = false;

    QString expr = m_expression.trimmed();

    if (expr.startsWith(QString("return")))  {
        isReturn = true;
        expr = expr.right(expr.length() - 6);
    }

    ifDebug(kDebug() << "expression: " << expr;)

    if (m_memberAccessOperation == StaticMemberAccess) {
        if (expr == "self" || expr == "parent") {
            // self and parent are only accessible from within a member function of a class
            if (DUContext* parent = context->parentContext()) {
                LOCKDUCHAIN;
                ClassDeclaration* classDec = dynamic_cast<ClassDeclaration*>(parent->owner());
                if (classDec) {
                    if (expr == "parent") {
                        FOREACH_FUNCTION(const BaseClassInstance& base, classDec->baseClasses) {
                            if (StructureType::Ptr classType = base.baseClass.type<StructureType>()) {
                                if (ClassDeclaration* baseClass = dynamic_cast<ClassDeclaration*>(classType->declaration(context->topContext()))) {
                                    if (baseClass->classType() == ClassDeclarationData::Class
                                            && baseClass->classModifier() != ClassDeclarationData::Abstract) {
                                        ifDebug(kDebug() << "correction: parent can do MemberAccess");
                                        m_parentAccess = true;
                                        m_memberAccessOperation = MemberAccess;
                                        m_expressionResult.setDeclaration(baseClass);
                                        break;
                                    }
                                }
                            }
                        }
                        if (!m_parentAccess) {
                            ifDebug(kDebug() << "class has no accessible parent class");
                            m_valid = false;
                            return;
                        }
                    } else {
                        m_expressionResult.setDeclaration(parent->owner());
                    }
                }
            }
        } else {
            QualifiedIdentifier id(expr);

            m_expressionResult.setDeclaration(findDeclarationImportHelper(duContext(), id, ClassDeclarationType, 0, 0));
        }
        if (m_expressionResult.type()) {
            ifDebug(kDebug() << "expression type: " << m_expressionResult.type()->toString();)
        } else {
            log(QString("expression \"%1\" could not be evaluated").arg(expr));
            m_valid = false;
            return;
        }
    } else {

        int offs = m_position.line - expr.count('\n');
        ExpressionParser expressionParser(offs);
        if (!expr.trimmed().isEmpty()) {
            m_expressionResult = expressionParser.evaluateType(expr.toUtf8(), m_duContext);
            if (m_expressionResult.type()) {
                LOCKDUCHAIN;
                ifDebug(kDebug() << "expression type: " << m_expressionResult.type()->toString();)
            } else {
                log(QString("expression \"%1\" could not be evaluated").arg(expr));
                m_valid = false;
                return;
            }
        }
    }

    if (m_memberAccessOperation == NoMemberAccess && !expr.trimmed().isEmpty()) {
        //This should never happen, because the position-cursor should be chosen at the beginning of a possible completion-context(not in the middle of a string)
        log(QString("Cannot complete \"%1\" because there is an expression without an access-operation").arg(expr));
        m_valid  = false;
    }
}

const QString& CodeCompletionContext::code() const
{
    return m_text;
}

void CodeCompletionContext::forbidIdentifier(const QString& identifier)
{
    QualifiedIdentifier id(identifier);

    ClassDeclaration *dec = dynamic_cast<ClassDeclaration*>(
                                findDeclarationImportHelper(m_duContext.data(), id, ClassDeclarationType, 0, 0)
                            );
    if (dec) {
        forbidIdentifier(dec);
    } else {
        // might be a class we are currently writing, i.e. without a proper declaration
        m_forbiddenIdentifiers << id.index();
    }
}

void CodeCompletionContext::forbidIdentifier(ClassDeclaration* klass)
{
    uint id;
    {
        LOCKDUCHAIN;
        // TODO: qualifiedIdentifier is marked as expensive - any other way
        //       we can do what we are doing here?
        // TODO: maybe we should clar the m_fobiddenIdentifiers after we got
        //       our list of items...
        id = klass->qualifiedIdentifier().index();
    }
    if (m_forbiddenIdentifiers.contains(id)) {
        // nothing to do
        return;
    }

    m_forbiddenIdentifiers << id;

    // add parents so those are excluded from the completion items as well
    if (klass->baseClassesSize() > 0) {
        FOREACH_FUNCTION(const BaseClassInstance& base, klass->baseClasses) {
            StructureType::Ptr type = base.baseClass.type<StructureType>();
            if (! type.isNull()) {
                ClassDeclaration* parent;
                {
                    LOCKDUCHAIN;
                    parent = dynamic_cast<ClassDeclaration*>(
                                               type->declaration(m_duContext->topContext())
                                           );
                }
                if (parent) {
                    forbidIdentifier(parent);
                }
            }
        }
    }
}

CodeCompletionContext::~CodeCompletionContext()
{
}

bool CodeCompletionContext::isValidPosition() const
{
    if (m_text.isEmpty())
        return true;

    //If we are in a string or comment, we should not complete anything
    /*  QString markedText = Utils::clearComments(m_text, '$');
      markedText = Utils::clearStrings(markedText,'$');

      if( markedText[markedText.length()-1] == '$' ) {
        //We are within a comment or string
        kDebug() << "code-completion position is invalid, marked text: \n\"" << markedText << "\"\n unmarked text:\n" << m_text << "\n";
        return false;
      }*/
    return true;
}

CodeCompletionContext::MemberAccessOperation CodeCompletionContext::memberAccessOperation() const
{
    return m_memberAccessOperation;
}

ExpressionEvaluationResult CodeCompletionContext::memberAccessContainer() const
{
    return m_expressionResult;
}

CodeCompletionContext* CodeCompletionContext::parentContext()
{
    return static_cast<CodeCompletionContext*>(KDevelop::CodeCompletionContext::parentContext());
}

QList<DUContext*> CodeCompletionContext::memberAccessContainers() const
{
    QList<DUContext*> ret;
    QList<AbstractType::Ptr> types;
    AbstractType::Ptr expressionTarget = m_expressionResult.type();
    if (expressionTarget.cast<UnsureType>()) {
        FOREACH_FUNCTION(const IndexedType& t, expressionTarget.cast<UnsureType>()->types) {
            types << t.abstractType();
        }
    } else {
        types << expressionTarget;
    }
    foreach (const AbstractType::Ptr &type, types) {
        const IdentifiedType* idType = dynamic_cast<const IdentifiedType*>(type.unsafeData());
        Declaration* declaration = 0;
        if (idType) {
            declaration = idType->declaration(m_duContext->topContext());
        }
        DUContext* ctx = 0;
        if (declaration) {
            ctx = declaration->logicalInternalContext(m_duContext->topContext());
        }
        if (ctx) {
            ret << ctx;
        } else if (declaration) {
            //Print some debug-output
            kDebug() << "Could not get internal context from" << declaration->toString();
        } else {
            //Print some debug-output
            kDebug() << "Could not get declaration";
        }
    }
    return ret;
}

QList<CompletionTreeItemPointer> CodeCompletionContext::completionItems(bool& abort, bool fullCompletion)
{
    LOCKDUCHAIN;

    QList<CompletionTreeItemPointer> items;

    if (!m_duContext)
        return items;

    typedef QPair<Declaration*, int> DeclarationDepthPair;

    if ( memberAccessOperation() == FileChoose ) {
        // file completion
        KUrl path = getUrlForBase(m_expression, m_duContext->url().toUrl().upUrl());
        KUrl base = path;
        if ( !m_expression.isEmpty() && !m_expression.endsWith('/') ) {
            base = base.upUrl();
        }
        base.cleanPath();
        IProject* project = ICore::self()->projectController()->findProjectForUrl(base);
        if ( project && !abort ) {
            QList<KUrl> addedUrls;
            foreach ( ProjectFolderItem* folder, project->foldersForUrl(base) ) {
                if ( abort ) {
                    break;
                }
                foreach ( ProjectFileItem* subFile, folder->fileList() ) {
                    if ( abort ) {
                        break;
                    }
                    if ( addedUrls.contains(subFile->url()) ) {
                        continue;
                    } else {
                        addedUrls << subFile->url();
                    }
                    IncludeItem item;
                    item.isDirectory = false;
                    item.basePath = base;
                    item.name = subFile->fileName();
                    items << CompletionTreeItemPointer(new IncludeFileItem(item));
                }
                foreach ( ProjectFolderItem* subFolder, folder->folderList() ) {
                    if ( abort ) {
                        break;
                    }
                    if ( addedUrls.contains(subFolder->url()) ) {
                        continue;
                    } else {
                        addedUrls << subFolder->url();
                    }
                    IncludeItem item;
                    item.isDirectory = true;
                    item.basePath = base;
                    item.name = subFolder->folderName();
                    items << CompletionTreeItemPointer(new IncludeFileItem(item));
                }
                if ( !folder->isProjectRoot() ) {
                    // expect a parent dir
                    IncludeItem item;
                    item.isDirectory = true;
                    item.basePath = base;
                    item.name = "..";
                    items << CompletionTreeItemPointer(new IncludeFileItem(item));
                }
            }
        }

        return items;
    } else if (memberAccessOperation() == ClassMemberChoose) {
        // get current class
        if (ClassDeclaration * currentClass = dynamic_cast<ClassDeclaration*>(m_duContext->owner())) {
            // whether we want to show a list of overloadable functions
            // i.e. not after we have typed one of the keywords var,const or abstract
            bool showOverloadable = true;
            // whether we want to remove static functions from the overloadable list
            // i.e. after we have typed "public function"
            bool filterStatic = false;
            // whether we want to remove non-static functions from the overloadable list
            // i.e. after we have typed "public static function"
            bool filterNonStatic = false;
            // private functions are always removed from the overloadable list
            // but when we type "protected function" only protected functions may be shown
            bool filterPublic = false;

            {
                // add typical keywords for class member definitions
                QStringList modifiers = getMethodTokens(m_text);

                // don't add keywords when "function" was already typed
                bool addKeywords = !modifiers.contains("function");

                if (currentClass->classModifier() == ClassDeclarationData::Abstract) {
                    // abstract is only allowed in abstract classes
                    if (modifiers.contains("abstract")) {
                        // don't show overloadable functions when we are defining an abstract function
                        showOverloadable = false;
                    } else if (addKeywords) {
                        ADD_KEYWORD("abstract");
                    }
                } else {
                    // final is only allowed in non-abstract classes
                    if (addKeywords && !modifiers.contains("final")) {
                        ADD_KEYWORD("final");
                    }
                }

                if (modifiers.contains("private")) {
                    // overloadable functions must not be declared private
                    showOverloadable = false;
                } else if (modifiers.contains("protected")) {
                    // only show protected overloadable methods
                    filterPublic = true;
                } else if (addKeywords && !modifiers.contains("public")) {
                    ADD_KEYWORD("public");
                    ADD_KEYWORD("protected");
                    ADD_KEYWORD("private");
                }

                if (modifiers.contains("static")) {
                    filterNonStatic = true;
                } else {
                    if (addKeywords) {
                        ADD_KEYWORD("static");
                    } else {
                        filterStatic = true;
                    }
                }

                if (addKeywords) {
                    ADD_KEYWORD("function");
                }

                if (modifiers.isEmpty()) {
                    // var and const may not have any modifiers
                    ADD_KEYWORD("var");
                    ADD_KEYWORD("const");
                }
            }
            kDebug() << showOverloadable;
            // complete overloadable methods from parents
            if (showOverloadable) {
                // TODO: use m_duContext instead of ctx
                // overloadable choose is only possible inside classes which extend/implement others
                if (currentClass->baseClassesSize()) {
                    DUContext* ctx = currentClass->internalContext();
                    if (!ctx) {
                        kDebug() << "invalid class context";
                        return items;
                    }
                    QList<uint> alreadyImplemented;
                    //TODO: always add __construct, __destruct and maby other magic functions
                    // get all visible declarations and add inherited to the completion items
                    foreach(const DeclarationDepthPair& decl, ctx->allDeclarations(ctx->range().end, m_duContext->topContext(), false)) {
                        if (decl.first->isFunctionDeclaration()) {
                            ClassFunctionDeclaration *method = dynamic_cast<ClassFunctionDeclaration*>(decl.first);
                            if (method) {
                                if (decl.second == 0) {
                                    // this function is already implemented
                                    alreadyImplemented << decl.first->indexedIdentifier().getIndex();
                                    continue;
                                }
                                // skip already implemented functions
                                if (alreadyImplemented.contains(decl.first->indexedIdentifier().getIndex())) {
                                    continue;
                                }
                                // skip non-static functions when requested
                                if (filterNonStatic && !method->isStatic()) {
                                    continue;
                                }
                                // skip static functions when requested
                                if (filterStatic && method->isStatic()) {
                                    continue;
                                }
                                // always skip private functions
                                if (method->accessPolicy() == Declaration::Private) {
                                    continue;
                                }
                                // skip public functions when requested
                                if (filterPublic && method->accessPolicy() == Declaration::Public) {
                                    // make sure no non-public base methods are added
                                    alreadyImplemented << decl.first->indexedIdentifier().getIndex();
                                    continue;
                                }
                                // skip final methods
                                if (method->isFinal()) {
                                    // make sure no non-final base methods are added
                                    alreadyImplemented << decl.first->indexedIdentifier().getIndex();
                                    continue;
                                }
                                // make sure we inherit or implement the base class of this method
                                if (!method->context() || !method->context()->owner()) {
                                    kDebug() << "invalid parent context/owner:" << method->toString();
                                    continue;
                                }
                                if (!currentClass->isPublicBaseClass(dynamic_cast<ClassDeclaration*>(method->context()->owner()),
                                                                        m_duContext->topContext())) {
                                    continue;
                                }

                                ImplementationItem::HelperType itype;
                                if (method->isAbstract()) {
                                    itype = ImplementationItem::Implement;
                                } else {
                                    itype = ImplementationItem::Override;
                                }

                                items << CompletionTreeItemPointer(new ImplementationItem(itype, DeclarationPointer(decl.first),
                                                                    KDevelop::CodeCompletionContext::Ptr(this), decl.second));
                                // don't add identical items twice to the completion choices
                                alreadyImplemented << decl.first->indexedIdentifier().getIndex();
                            }
                        }
                    }
                }
            } else {
                kDebug() << "invalid owner declaration for overloadable completion";
            }
        }
    } else if (m_expressionResult.type()) {
        QList<DUContext*> containers = memberAccessContainers();
        kDebug() << "containers: " << containers.count();
        if (!containers.isEmpty()) {
            // get the parent class when we are inside a method declaration
            ClassDeclaration* currentClass = 0;
            if (m_duContext->owner() && m_duContext->owner()->isFunctionDeclaration() &&
                    m_duContext->parentContext() && m_duContext->parentContext()->owner()) {
                currentClass = dynamic_cast<ClassDeclaration*>(m_duContext->parentContext()->owner());
            }

            bool filterAbstract = memberAccessOperation() == StaticMemberAccess || memberAccessOperation() == MemberAccess;

            foreach(DUContext* ctx, containers) {
                ClassDeclaration* accessedClass = dynamic_cast<ClassDeclaration*>(ctx->owner());
                if (abort)
                    return items;

                foreach(const DeclarationDepthPair& decl, ctx->allDeclarations(ctx->range().end, m_duContext->topContext(), false)) {
                    //If we have StaticMemberAccess, which means A::Bla, show only static members, except if we're within a class that derives from the container
                    ClassMemberDeclaration* classMember = dynamic_cast<ClassMemberDeclaration*>(decl.first);
                    if (memberAccessOperation() != StaticMemberAccess) {
                        if (classMember && classMember->isStatic())
                            continue; //Skip static class members when not doing static access
                    } else {
                        if (!classMember || !classMember->isStatic())
                            continue; //Skip static class members when not doing static access
                    }

                    // check access policy
                    if (classMember && accessedClass) {
                        // by default only show public declarations
                        Declaration::AccessPolicy ap = Declaration::Public;
                        if (currentClass) {
                            // if we are inside a class, we might want to show protected or private members
                            ClassDeclaration* memberClass = dynamic_cast<ClassDeclaration*>(classMember->context()->owner());
                            if (memberClass) {
                                if (currentClass == accessedClass) {
                                    if (currentClass == memberClass) {
                                        // we can show all members of the current class
                                        ap = Declaration::Private;
                                    } else if (currentClass->isPublicBaseClass(memberClass, m_duContext->topContext())) {
                                        // we can show all but private members of ancestors of the current class
                                        ap = Declaration::Protected;
                                    }
                                } else if (currentClass->isPublicBaseClass(accessedClass, m_duContext->topContext())
                                            && (accessedClass == memberClass ||
                                                accessedClass->isPublicBaseClass(memberClass, m_duContext->topContext()))) {
                                    // we can show all but private members of ancestors of the current class
                                    ap = Declaration::Protected;
                                }
                            }
                        }
                        if (ap < classMember->accessPolicy()) {
                            continue;
                        }
                    }

                    // filter abstract methods
                    if (filterAbstract) {
                        ClassFunctionDeclaration* method = dynamic_cast<ClassFunctionDeclaration*>(decl.first);
                        if (method && method->isAbstract()) {
                            continue;
                        }
                    }

                    if (!decl.first->identifier().isEmpty())
                        items << CompletionTreeItemPointer(new NormalDeclarationCompletionItem(DeclarationPointer(decl.first), KDevelop::CodeCompletionContext::Ptr(this), decl.second));
                }
            }
        } else {
            kDebug() << "setContext: no container-type";
        }

    } else {
        //Show all visible declarations
        QSet<uint> existingIdentifiers;
        QList<DeclarationDepthPair> decls = m_duContext->allDeclarations(m_duContext->type() == DUContext::Class ? m_duContext->range().end : m_position, m_duContext->topContext());

        kDebug() << "setContext: using all declarations visible:" << decls.size();

        QListIterator<DeclarationDepthPair> i(decls);
        i.toBack();
        while (i.hasPrevious()) {
            DeclarationDepthPair decl = i.previous();
            Declaration* dec = decl.first;
            if (dec->kind() == Declaration::Instance) {
                // filter non-superglobal vars of other contexts
                if (dec->context() != m_duContext.data()) {
                    VariableDeclaration* vDec = dynamic_cast<VariableDeclaration*>(dec);
                    if ( vDec && !vDec->isSuperglobal() ) {
                        continue;
                    }
                }

                if (existingIdentifiers.contains(dec->indexedIdentifier().getIndex())) continue;
                existingIdentifiers.insert(dec->indexedIdentifier().getIndex());
            }
            if (abort)
                return items;
            if (!isValidCompletionItem(dec)) continue;
            items << CompletionTreeItemPointer(new NormalDeclarationCompletionItem(DeclarationPointer(dec), KDevelop::CodeCompletionContext::Ptr(this), decl.second));
        }
        uint count = 0;
        const CodeModelItem* foundItems = 0;
        foreach(QSet<IndexedString> urlSets, completionFiles()) {
            foreach(const IndexedString &url, urlSets) {
                CodeModel::self().items(url, count, foundItems);
                for (uint i = 0; i < count; ++i) {
                    if (foundItems[i].kind == CodeModelItem::Class || foundItems[i].kind == CodeModelItem::Function) {
                        foreach(ParsingEnvironmentFilePointer env, DUChain::self()->allEnvironmentFiles(url)) {
                            if (env->language() != IndexedString("Php")) continue;
                            TopDUContext* top = env->topContext();
                            if(!top) continue;
                            if (m_duContext->imports(top)) continue;
                            QList<Declaration*> decls = top->findDeclarations(foundItems[i].id);
                            foreach(Declaration* decl, decls) {
                                if (abort) return items;
                                // we don't want to have class methods, just normal functions
                                if ( foundItems[i].kind == CodeModelItem::Function &&
                                     decl->context() && decl->context()->type() == DUContext::Class ) {
                                    continue;
                                }
                                if (!isValidCompletionItem(decl)) continue;
                                items << CompletionTreeItemPointer(new NormalDeclarationCompletionItem(DeclarationPointer(decl), KDevelop::CodeCompletionContext::Ptr(this)));
                            }
                        }
                    }
                }
            }
        }
    }

    ///Find all recursive function-calls that should be shown as call-tips
    CodeCompletionContext::Ptr parentContext(this);
    do {
        if (abort)
            return items;

        parentContext = parentContext->parentContext();
        if (parentContext) {
            if (parentContext->memberAccessOperation() == CodeCompletionContext::FunctionCallAccess) {
                if (!parentContext->memberAccessContainer().allDeclarationIds().isEmpty()) {
                    Declaration* decl = parentContext->memberAccessContainer().allDeclarationIds().first().getDeclaration(m_duContext->topContext());
                    if (!isValidCompletionItem(decl)) continue;
                    items << CompletionTreeItemPointer(new NormalDeclarationCompletionItem(DeclarationPointer(decl), KDevelop::CodeCompletionContext::Ptr::staticCast(parentContext)));
                }
            } else {
                kDebug() << "parent-context has non function-call access type";
            }
        }
    } while (parentContext);

    return items;
}

inline bool CodeCompletionContext::isValidCompletionItem(Declaration* dec)
{
    static ClassDeclaration* exceptionDecl;
    if (!exceptionDecl) {
        QList<Declaration*> decs = dec->context()->findDeclarations(QualifiedIdentifier("Exception"));
        Q_ASSERT(decs.count() == 1);
        exceptionDecl = dynamic_cast<ClassDeclaration*>(decs.first());
        Q_ASSERT(exceptionDecl);
    }

    if (m_memberAccessOperation == ExceptionChoose
            || m_memberAccessOperation == NewClassChoose
            || m_memberAccessOperation == InterfaceChoose
            || m_memberAccessOperation == ClassExtendsChoose) {
        // filter current class
        if (!m_forbiddenIdentifiers.isEmpty() && m_forbiddenIdentifiers.contains(dec->qualifiedIdentifier().index())) {
            return false;
        }
        ClassDeclaration* classDec = dynamic_cast<ClassDeclaration*>(dec);
        // filter non-classes
        if (!classDec) {
            return false;
        }
        // show non-interface and non-abstract
        else if (m_memberAccessOperation == NewClassChoose) {
            return !(classDec->classModifier() & ClassDeclarationData::Abstract)
                   && classDec->classType() == ClassDeclarationData::Class;
        }
        // filter non-exception classes
        else if (m_memberAccessOperation == ExceptionChoose) {
            return classDec->equalQualifiedIdentifier(exceptionDecl)
                   || classDec->isPublicBaseClass(exceptionDecl, m_duContext->topContext());
        }
        // show interfaces
        else if (m_memberAccessOperation == InterfaceChoose) {
            return classDec->classType() == ClassDeclarationData::Interface;
        }
        // show anything but final classes and interfaces
        else if (m_memberAccessOperation == ClassExtendsChoose) {
            return !(classDec->classModifier() & ClassDeclarationData::Final)
                   && classDec->classType() == ClassDeclarationData::Class;
        }
    }
    if (m_memberAccessOperation == ExceptionInstanceChoose) {
        if (dec->kind() != Declaration::Instance) return false;
        StructureType::Ptr structType = dec->type<StructureType>();
        if (!structType) return false;
        ClassDeclaration* classDec = dynamic_cast<ClassDeclaration*>(structType->declaration(dec->topContext()));
        if (!classDec) return false;
        return classDec->isPublicBaseClass(exceptionDecl, m_duContext->topContext());
    }

    if (m_memberAccessOperation == NoMemberAccess) {
        // filter private methods and class members when doing a global completion
        // when we are inside a class function, don't filter the private stuff
        // of the current class
        if ( ClassMemberDeclaration* memberDec = dynamic_cast<ClassMemberDeclaration*>(dec) ) {
            if ( memberDec->accessPolicy() == Declaration::Private
                    && memberDec->context() != m_duContext->parentContext() ) {
                return false;
            }
        } else if ( ClassFunctionDeclaration* funDec = dynamic_cast<ClassFunctionDeclaration*>(dec) ) {
            if ( funDec->accessPolicy() == Declaration::Private
                    && memberDec->context() != m_duContext->parentContext() ) {
                return false;
            }
        }
    }

    return true;
}

QList<QSet<IndexedString> > CodeCompletionContext::completionFiles()
{
    QList<QSet<IndexedString> > ret;
    if (ICore::self()) {
        foreach(IProject* project, ICore::self()->projectController()->projects()) {
            ret << project->fileSet();
        }
    }
    return ret;
}

}
