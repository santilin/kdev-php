/*
 * KDevelop Php Code Completion Support
 *
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
 * Copyright 2008 Hamish Rodda <rodda@kde.org>
 * Copyright 2008 Niko Sams <niko.sams@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "item.h"

#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <ktexteditor/range.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <language/codecompletion/codecompletionmodel.h>
#include <language/codecompletion/codecompletionhelper.h>
#include <language/duchain/declaration.h>
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/namespacealiasdeclaration.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/types/functiontype.h>
#include <language/duchain/types/structuretype.h>
#include <language/codecompletion/codecompletionhelper.h>
#include <language/duchain/classdeclaration.h>

#include "completion/helpers.h"
#include "../duchain/navigation/navigationwidget.h"
#include "../duchain/constantdeclaration.h"
#include "../duchain/variabledeclaration.h"

using namespace KDevelop;

namespace Php {

QString NormalDeclarationCompletionItem::declarationName() const {
  if (m_declaration->identifier().toString().isEmpty())
    return "<unknown>";
  else {
    QString ret = m_declaration->identifier().toString();
    if ( !m_declaration->isFunctionDeclaration() ) {
      if (dynamic_cast<VariableDeclaration*>(m_declaration.data())) {
        ret = "$" + ret;
      } else if ( ClassMemberDeclaration* memberDec = dynamic_cast<ClassMemberDeclaration*>(m_declaration.data())) {
        if ( memberDec->isStatic() && memberDec->abstractType() && ! memberDec->abstractType()->modifiers() & AbstractType::ConstModifier ) {
          // PHP is strange, $obj->asdf, class::const but class::$static ...
          ret = "$" + ret;
        }
      }
    }
    return ret;
  }
}

void NormalDeclarationCompletionItem::executed(KTextEditor::Document* document, const KTextEditor::Range& word)
{
  if( m_declaration && dynamic_cast<AbstractFunctionDeclaration*>(m_declaration.data()) ) {
    //Do some intelligent stuff for functions with the parens:
    insertFunctionParenText(document, word, m_declaration);
  }
}

QVariant NormalDeclarationCompletionItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {

  DUChainReadLocker lock(DUChain::lock(), 500);
  if(!lock.locked()) {
    kDebug() << "Failed to lock the du-chain in time";
    return QVariant();
  }

  if(!m_declaration) {
    return QVariant();
  }
  
  Declaration* dec = const_cast<Declaration*>( m_declaration.data() );

  switch (role) {
    case CodeCompletionModel::ItemSelected:
       return QVariant(NavigationWidget::shortDescription(dec));
    case Qt::DisplayRole:
      switch (index.column()) {
        case CodeCompletionModel::Prefix:
        {
          if( dec->kind() == Declaration::Type && !dec->type<FunctionType>() && !dec->isTypeAlias() ) {
            if (StructureType::Ptr classType =  dec->type<StructureType>()) {
              ClassDeclaration* classDec = dynamic_cast<ClassDeclaration*>(dec);
              if (classDec) {
                if (classDec->classType() == ClassDeclarationData::Interface) {
                  return "interface";
                } else {
                  return "class";
                }
              }
            }
            return QVariant();
          }
        }

        case CodeCompletionModel::Arguments:
          if (FunctionType::Ptr functionType = dec->type<FunctionType>()) {
            QString ret;
            createArgumentList(*this, ret, 0);
            return ret;
          }
        break;
      }
      break;
    case CodeCompletionModel::HighlightingMethod:
    if( index.column() == CodeCompletionModel::Arguments ) {
      if( completionContext()->memberAccessOperation() == CodeCompletionContext::FunctionCallAccess ) {
        return QVariant(CodeCompletionModel::CustomHighlighting);
      }else{
        return QVariant();
      }
      break;
    }

    break;

    case CodeCompletionModel::CustomHighlight:
    if( index.column() == CodeCompletionModel::Arguments && completionContext()->memberAccessOperation() == CodeCompletionContext::FunctionCallAccess ) {
      QString ret;
      QList<QVariant> highlight;
      createArgumentList(*this, ret, &highlight);
      return QVariant(highlight);
    }
    break;
  }
  lock.unlock();

  return KDevelop::NormalDeclarationCompletionItem::data(index, role, model);
}

QWidget* NormalDeclarationCompletionItem::createExpadingWidget(const KDevelop::CodeCompletionModel* model) const
{
  return new NavigationWidget(m_declaration, model->currentTopContext());
}

bool NormalDeclarationCompletionItem::createsExpadingWidget() const
{
  return true;
}

KSharedPtr<CodeCompletionContext> NormalDeclarationCompletionItem::completionContext() const {
  return KSharedPtr<CodeCompletionContext>::staticCast(m_completionContext);
}


}

