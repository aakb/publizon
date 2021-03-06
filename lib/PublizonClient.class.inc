<?php
/**
 * @file
 * Implementation of the main communication object with the Publizon
 * web-service.
 */

/**
 * This is a single ton class that is used to communicate with the Publizon
 * web-service.
 *
 * You get a instances of the object by calling PublizonClient::getClient();
 */
class PublizonClient {
  private $retailer_id;
  private $retailer_key_code;
  private $langcode;
  private $logger;

  private $base_url = '';
  private $end_points = array(
    'product' => 'getproduct.asmx',
    'product_list' => 'getproductlist.asmx',
    'loan' => 'createloan.asmx',
    'user' => 'validatelibraryuser.asmx',
    'libraray_list' => 'getlibrarylist.asmx',
    'library_user_order_list' => 'getlibraryuserorderlist.asmx',
    'removed_product_list' => 'getremovedproductlist.asmx',
    'library_profile' => 'getlibraryinfo.asmx',
  );

  // Holds the object instance (part of the singleton pattern).
  private static $instance = NULL;

  /**
   * Class constructor that prepares an connection to publizon and wrappers all
   * calls into this object. This is a private constructor as the object is
   * implemented as a singleton pattern, use PublizonClient::getClient() to get
   * an instance.
   *
   * If the parameters is not set (NULL) the object will try to use user provied
   * information stored in the users current session. If there non given and no
   * session it will fallback to the information given in the administration
   * form.
   *
   * @param string $retailer_id
   *   An id that represents the library at publizon.
   * @param string $retailer_key_code
   *   An hash value validating the library as publizon.
   * @param string $langcode
   *   The language code used to fetch the information ('da').
   * @param PublizonLogger $logger
   *   Publizon logger class.
   */
  private function __construct($retailer_id, $retailer_key_code, $langcode, $logger) {
    // Get publizon settings from the administration interface.
    $publizon = variable_get('publizon', FALSE);
    if (!$publizon) {
      throw new PublizonException('Publizon client could not load default settings.');
    }

    // Set base URL.
    $this->base_url = $publizon['settings']['base_url'];

    // Encode retailer key code (if needed).
    $retailer_key_code = isset($retailer_key_code) ? md5($retailer_key_code) : NULL;

    // Set configuration.
    $this->setSetting('langcode', $langcode);
    $this->setSetting('retailer_id', $retailer_id);
    $this->setSetting('retailer_key_code', $retailer_key_code);

    // Set logger.
    $this->logger = new PublizonVoidLogger();
    if (!isset($logger)) {
      if ($publizon['settings']['logging']) {
        $this->logger = new PublizonWatchDogLogger();
      }
    }
  }

  /**
   * Returns an instances of the PublizonClient object that can be used to
   * communicate with the Publizon web-service.
   *
   * @see PublizonClient().
   *
   * @param string $retailer_id
   *   An id that represents the library at publizon.
   * @param string $retailer_key_code
   *   An hash value validating the library as publizon.
   * @param string $langcode
   *   The language code used to fetch the information ('da').
   * @param PublizonLogger $logger
   *   Publizon logger class.
   */
  public static function getClient($retailer_id = NULL, $retailer_key_code = NULL, $langcode = NULL, $logger = NULL) {
    if (!isset(self::$instance)) {
      $class = __CLASS__;
      self::$instance = new $class($retailer_id, $retailer_key_code, $langcode, $logger);
    }
    return self::$instance;
  }

  /**
   * Create a new loan at Publizon. It throws an exception if the loan does not
   * go through else a download url is returned.
   *
   * @param string $card_number
   *   The users card number/CPR also known as login id.
   * @param string $pincode
   *   The users login pin code.
   * @param string $isbn
   *   ISBN number on the product.
   *
   * @return string
   *   Download URL to the product.
   *
   * @throws PublizonException
   */
  public function createLoan($card_number, $pincode, $isbn) {
    // Make request to the web-service for the loan. It throws an exception if
    // the loan fails.
    $response = $this->call('loan', 'CreateLoan', array(
      'pub:ebookid' => $isbn,
      'pub:cardnumber' => $card_number,
      'pub:pincode' => $pincode,
    ), array('pub' => 'pubhub.dk'));

    $data = $response->xpath('//data');
    if (isset($data[0])) {
      $data = $data[0];
      return (string) $data->downloadurl;
    }

    $this->logger->log('Create loan request returned no data (' . $isbn . ').', 'ERROR');
    throw new PublizonException('Create loan request returned no data (' . $isbn . ').');
  }

  public function getLastLoans() {
    throw new PublizonException('Not implemented yet');
  }

  public function getLastLoansByLibrary() {
    throw new PublizonException('Not implemented yet');
  }

  /**
   * Get list of last loaned products for a given type of product.
   *
   * @todo Get Publizon to make it possible to limit the number of elements in
   *       the response.
   *
   * @param int $list_type
   *   This can either be PUBLIZON_EBOOK or PUBLIZON_NETSOUND.
   * @param int $limit
   *   The number of products that should be returned. If the limit can not be
   *   reached, the number available will be returned.
   *
   * @return array
   *   Array with PublizonProduct objects
   *
   * @throws PublizonException
   *   Will be throw if the list type is unknown.
   */
  public function getLastLoansForAll($list_type, $limit = 7) {
    if ($list_type == PUBLIZON_EBOOK || $list_type == PUBLIZON_NETSOUND) {
      // Make request to the web-service for the loans.
      $response = $this->call('libraray_list', 'GetLastLoansForAll', array(
        'pub:listtype' => $list_type,
      ), array('pub' => 'http://pubhub.dk/'));

      $products = array();
      $items_found = 0;

      // Convert XML into PublizonProduct objects, if data was returned.
      $data = $response->xpath('//data');
      if (isset($data[0])) {
        $data = $data[0];
        foreach ($data->orderinformationitem as $item) {
          $product = new PublizonProduct((string) $item->identifier);
          if (isset($product->status)) {
            $products[] = $product;
            $items_found++;
          }
          // If we found the number of products we are looking for, break.
          if ($items_found == $limit) {
            break;
          }
        }
      }

      return $products;
    }
    $this->logger->log('Request for unsuported loans type ' . check_plain($list_type), 'ERROR');
    throw new PublizonException('You are trying to get unsupported type of loans from Publizon');
  }

  public function getTopList() {
    throw new PublizonException('Not implemented yet');
  }

  /**
   * Retrieves information about the user loans stored at Publizon.
   *
   * @param string $card_number
   *   User card_number / CPR also known as login id.
   * @param string $list_type
   *   This can either be PUBLIZON_EBOOK or PUBLIZON_NETSOUND.
   *
   * @return bool
   *   Returns an array with general information about the user and the user
   *   loans. If no information is returned from the web-service FALSE.
   *
   * @throws PublizonException
   */
  public function getLibraryUserOrderList($card_number, $list_type) {
    if ($list_type == PUBLIZON_EBOOK || $list_type == PUBLIZON_NETSOUND) {
      // Make request to the web-service for the loans.
      $response = $this->call('library_user_order_list', 'GetLibraryUserOrderList', array(
        'pub:cardnumber' => $card_number,
        'pub:booktype' => $list_type,
      ), array('pub' => 'http://pubhub.dk/'));

      // Check if any data was returned.
      $data = $response->xpath('//data');
      $extension = $response->xpath('//LibraryExtension');
      if (isset($data[0]) && isset($extension[0])) {
        return array($data[0], $extension[0]);
      }

      $this->logger->log('No loans information was returned for the user', 'WARNING');
      return FALSE;
    }
    $this->logger->log('Request for unsuported loans type ' . check_plain($list_type), 'ERROR');
    throw new PublizonException('You are trying to get unsupported type of loans from Publizon');
  }

  /**
   * Fetch XML representation for a product at publizon.
   *
   * You should not use this function directly, but simple create a new
   * PublizonProduct object. Which will automatically fetch the product through
   * this function.
   *
   * @see PublizonProduct()
   *
   * @param string $isbn
   *   Product id also know as ISBN number.
   *
   * @return SimpleXmlElement
   *   Raw XML object containing the product.
   */
  public function getProduct($isbn) {
    // Get product form the web-serivce.
    $response = $this->call('product', 'GetProduct', array(
      'pub:ebookid' => $isbn,
    ), array('pub' => 'http://pubhub.dk/'));

    // Check if any data was returned.
    $data = $response->xpath('//data');
    if (isset($data[0])) {
      return $data[0];
    }

    $this->logger->log('The product with isbn (' . $isbn . ') did not return any data', 'WARNING');
    return FALSE;
  }

  public function getProductList() {
    throw new Exception('Not implemented yet');
  }

  public function getRemovedProductList() {
    throw new Exception('Not implemented yet');
  }

  /**
   * Validates that a given user is allowed to login to the publizon
   * web-service.
   *
   * @param string $card_number
   *   User card_number / CPR also known as login id.
   * @param string $pincode
   *   The users pin code.
   *
   * @return bool
   *   If the user is valid TRUE is return else FALSE.
   */
  public function validateLibraryUser($card_number, $pincode) {
    try {
      $response = $this->call('user', 'ValidateLibraryUser', array(
        'pub:cardnumber' => $card_number,
        'pub:pincode' => $pincode,
      ), array('pub' => 'http://pubhub.dk/'));
    }
    catch (PublizonException $e) {
      $this->logger->log($e->getMessage(), 'ERROR');
      return FALSE;
    }

    // Check if this is a special testcard and set by pass cache session. There
    // is no problem in using the session here, in relation to revers proxy, as
    // sessions will be used for logged in user any way.
    $data = $response->xpath('//data');
    $_SESSION['by_pass_cache'] = FALSE;
    if ((string) $data[0]->testcard == 'true') {
      $_SESSION['by_pass_cache'] = TRUE;
    }
    return TRUE;
  }

  /**
   * Get basic information about a given library as registered at Publizon.
   *
   * It will fallback on the clients retailer id and retailer key code defined
   * in the current instance of the client if non is given as arguments.
   *
   * @param string $retailer_id
   *   The libraries ID at Publizon.
   * @param string $retailer_key_code
   *   The libraries access code at Publizon.
   *
   * @return bool
   */
  public function getLibraryProfile($retailer_id = NULL, $retailer_key_code = NULL) {
    // Try to get parameters and fallback to default.
    $retailer_id = isset($retailer_id) ? $retailer_id : $this->retailer_id;
    $retailer_key_code = isset($retailer_key_code) ? $retailer_key_code : $this->retailer_key_code;

    // Make the web-service call.
    $response = $this->call('library_profile', 'GetLibraryProfile', array(
        'pub:retailerid' => $retailer_id,
        'pub:retailerkeycode' => $retailer_key_code,
    ), array('pub' => 'http://pubhub.dk/'));

    // Check if any data was returned.
    $data = $response->xpath('//data');
    if (isset($data[0])) {
      return $data[0];
    }

    $this->logger->log('The library profile with id (' . $retailer_id . ') did not return any data', 'WARNING');
    return FALSE;
  }

  /**
   * Wrapper function for preforming of call to Publizon's web-service via the
   * NanoSOAP module.
   *
   * @param string $endpoint
   *   Index into the end-points array (defined in the top of the object).
   * @param string $action
   *   SOAP action to request.
   * @param array $parameters
   *   The parameters to call the actions with. Also known as the content of the
   *   message. Remember to prefix them with the namespace they should use.
   * @param array $namespace
   *   The namespaces that is used inside the SOAP message.
   *
   * @return SimpleXMLElement
   *   The response from the web-service.
   *
   * @throws PublizonConnectionException
   *   If the connection fails.
   * @throws PublizonException
   *   If the response dose not have a 101 status.
   */
  private function call($endpoint, $action, $parameters, $namespace) {
    // Log request.
    $this->logger->log('Call to "' . $this->end_points[$endpoint] . '" for "' . $action . '"');

    // Get namespace key.
    $ns = array_keys($namespace);

    // Create a new soap client for the request and set default parameters.
    $client = new PublizonNanoSOAPClient($this->base_url . $this->end_points[$endpoint], array('namespaces' => $namespace));
    $parameters += array(
      $ns[0] . ':retailerid' => $this->retailer_id,
      $ns[0] . ':retailerkeycode' => $this->retailer_key_code,
      $ns[0] . ':languagecode' => $this->langcode,
    );

    // Make the request.
    try {
      $response = $client->call($ns[0] . ':' . $action, $parameters);
      $response = simplexml_load_string($response);
    }
    catch (Exception $e) {
      // Connection error.
      $this->logger->log('Connection with the Publizon web-service failed.');
      throw new PublizonConnectionException('Connection with the Publizon web-service failed.');
    }

    // Check the response error codes.
    $status = $response->xpath('//status');
    if (isset($status[0]) && $status[0]->code != 101) {
      if (isset($status[0]->message)) {
        $this->logger->log(check_plain($status[0]->message));
      }
      else {
        $this->logger->log('Connection with the Publizon web-service failed.');
      }

      // Throw exception, it's translated in the exception.
      throw new PublizonException($status[0]->message, intval($status[0]->code));
    }

    return $response;
  }

  /**
   * Used to set a object setting for communication with the Publizon
   * web-service. If value is given it's used, if non is given the session will
   * be checked and then the default configuration.
   *
   * Data in the session indicates that the current use is logged in, hence this
   * should be used.
   *
   * @param string $name
   *   Name of the setting that should be set.
   * @param string $value
   *   The value that should be set.
   *
   * @throws PublizonException
   *   If the function falls back to using administration setting and this could
   *   not be found exception is throw.
   */
  private function setSetting($name, $value = NULL) {
    if (isset($value)) {
      $this->$name = $value;
    }
    elseif (isset($_SESSION[$name])) {
      $this->$name = $_SESSION[$name];
    }
    else {
      $publizon = variable_get('publizon', FALSE);
      if (!$publizon) {
        throw new PublizonException('Publizon client could not load default settings for "' . $name . '".');
      }
      $this->$name = $publizon['settings'][$name];
    }
  }
}

/**
 * Custom Exception that ensures that the string given is checked and
 * translatable in the front ned.
 *
 * All other Exception class should use this as their base class.
 */
class PublizonException extends Exception {
  /**
   * Create new constructor that ensure message is translated.
   */
  public function __construct($message, $code = 0, Exception $previous = NULL) {
    // Add support for tranlation.
    $message = t(check_plain($message));

    // Make sure everything is assigned properly.
    parent::__construct($message, $code, $previous);
  }
}

/**
 * Connection Exception that is used when the communication with Publizon fails.
 *
 * Mostly in the case where they are not online.
 */
class PublizonConnectionException extends PublizonException {}
