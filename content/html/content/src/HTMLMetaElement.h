/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLMetaElement_h
#define mozilla_dom_HTMLMetaElement_h

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLMetaElement.h"

namespace mozilla {
namespace dom {

class HTMLMetaElement : public nsGenericHTMLElement,
                        public nsIDOMHTMLMetaElement
{
public:
  HTMLMetaElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~HTMLMetaElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLMetaElement
  NS_DECL_NSIDOMHTMLMETAELEMENT

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);
  void CreateAndDispatchEvent(nsIDocument* aDoc, const nsAString& aEventName);

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsIDOMNode* AsDOMNode() { return this; }

  // XPCOM GetName is fine.
  void SetName(const nsAString& aName, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::name, aName, aRv);
  }
  // XPCOM GetHttpEquiv is fine.
  void SetHttpEquiv(const nsAString& aHttpEquiv, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::httpEquiv, aHttpEquiv, aRv);
  }
  // XPCOM GetContent is fine.
  void SetContent(const nsAString& aContent, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::content, aContent, aRv);
  }
  // XPCOM GetScheme is fine.
  void SetScheme(const nsAString& aScheme, ErrorResult& aRv)
  {
    SetHTMLAttr(nsGkAtoms::scheme, aScheme, aRv);
  }

  virtual JSObject*
  WrapNode(JSContext* aCx, JSObject* aScope, bool* aTriedToWrap) MOZ_OVERRIDE;

protected:
  virtual void GetItemValueText(nsAString& text);
  virtual void SetItemValueText(const nsAString& text);
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_HTMLMetaElement_h
