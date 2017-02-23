import time
import os
import shutil

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
chrome_options = Options()
testdir = os.path.dirname(os.path.abspath(__file__))
chrome_options.add_argument("nwapp=" + testdir)
user_data_dir = os.path.join(testdir, 'userdata')
chrome_options.add_argument("user-data-dir=" + user_data_dir)
try:
    shutil.rmtree(user_data_dir)
except:
    pass

def checkResults(driver):
    result = driver.find_element_by_id('result1').get_attribute('innerHTML')
    print result
    assert("success 1" in result)
    result = driver.find_element_by_id('result2').get_attribute('innerHTML')
    print result
    assert("success 2" in result)
    result = driver.find_element_by_id('result3').get_attribute('innerHTML')
    print result
    assert("success 3" in result)

driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)

try:
  
#1st run, create the database
    driver.implicitly_wait(10)
    print driver.current_url
    button = driver.find_element_by_id('Create')
    button.click() # click the button
    checkResults(driver)
    driver.close()
    driver.quit()
    
#2nd run, try to reopen the database, should be success
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Read')
    button.click() # click the button
    checkResults(driver)
    driver.close()
    driver.quit()
    shutil.rmtree(user_data_dir)

#3rd run, create the encrypted database
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Create-Encrypted')
    button.click() # click the button
    checkResults(driver)
    driver.close()
    driver.quit()
    
#4th run, try to reopen the encrypted database, should be success
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Read-Encrypted')
    button.click() # click the button
    checkResults(driver)

finally:
    driver.close()
    driver.quit()
    shutil.rmtree(user_data_dir)
