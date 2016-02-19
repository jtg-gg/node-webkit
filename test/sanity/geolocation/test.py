import time
import os
import pyautogui

from selenium import webdriver
from selenium.webdriver.chrome.options import Options

chrome_options = Options()
chrome_options.add_argument("nwapp=" + os.path.dirname(os.path.abspath(__file__)))

driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
try:
    driver.implicitly_wait(15)
    print driver.current_url
    button = driver.find_element_by_id('test1')
    print button.text
    button.click() # click the button

    try:
        result = driver.find_element_by_id('result1')
    except:
        pyautogui.press('enter') #click the "allow" button for OSX if it asks for user permission
        result = driver.find_element_by_id('result1')

    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))
finally:
  driver.quit()
