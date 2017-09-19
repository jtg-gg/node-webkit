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

    print "test 1"
    driver.execute_script("getMetric();")
    init_x, init_y, init_width, init_height = driver.execute_script("return winMetrics;")
    print "x:%d, y:%d, width:%d, height:%d" % (init_x, init_y, init_width, init_height)
    
    button = driver.find_element_by_id('newWindow')
    button.click() # click the button

    pyautogui.moveTo(init_x + 25, init_y + 25)
    pyautogui.click();
    result = driver.find_element_by_id('result1')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    print "test 2"
    pyautogui.moveTo(init_x + 12, init_y + 12)
    pyautogui.click();
    result = driver.find_element_by_id('result2')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

finally:
  driver.quit()
