/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsParentalControlsService_h_
#define nsParentalControlsService_h_

#include "nsIParentalControlsService.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"


class nsParentalControlsService : public nsIParentalControlsService {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPARENTALCONTROLSSERVICE

  nsParentalControlsService();

 protected:
  virtual ~nsParentalControlsService();

 private:
  bool mEnabled;
};

#endif /* nsParentalControlsService_h_ */
