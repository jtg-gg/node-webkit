import time
import os

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
chrome_options = Options()
chrome_options.add_argument("nwapp=" + os.path.dirname(os.path.abspath(__file__)))

driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
try:
    driver.implicitly_wait(90)
    print driver.current_url
    button = driver.find_element_by_id('test1')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result1')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test2')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result2')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test3')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result3')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test4')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result4')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test5')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result5')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test6')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result6')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test7')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result7')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test8')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result8')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    button = driver.find_element_by_id('test9')
    print button.text
    button.click() # click the button
    result = driver.find_element_by_id('result9')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

finally:
    driver.quit()
