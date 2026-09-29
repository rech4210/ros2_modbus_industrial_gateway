import React, { createContext, useContext, useState, useEffect } from 'react';
import { HmiLocale } from '../locales/types';
import { koLocale } from '../locales/ko';
import { enLocale } from '../locales/en';

type Language = 'KO' | 'EN';

interface I18nContextType {
  language: Language;
  setLanguage: (lang: Language) => void;
  t: HmiLocale;
}

const I18nContext = createContext<I18nContextType>({
  language: 'KO',
  setLanguage: () => {},
  t: koLocale,
});

export const I18nProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [language, setLanguageState] = useState<Language>(() => {
    const saved = localStorage.getItem('hmi_language');
    return saved === 'EN' ? 'EN' : 'KO';
  });

  const setLanguage = (lang: Language) => {
    setLanguageState(lang);
    localStorage.setItem('hmi_language', lang);
  };

  const t = language === 'KO' ? koLocale : enLocale;

  return (
    <I18nContext.Provider value={{ language, setLanguage, t }}>
      {children}
    </I18nContext.Provider>
  );
};

export const useI18n = () => useContext(I18nContext);
