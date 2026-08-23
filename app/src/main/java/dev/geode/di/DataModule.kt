package dev.geode.di

import android.content.Context
import android.content.SharedPreferences
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import dev.geode.data.GeodePrefsFiles
import dev.geode.geodeContainer
import dev.geode.ui.UserDataRepository
import javax.inject.Qualifier
import javax.inject.Singleton

@Qualifier
@Retention(AnnotationRetention.BINARY)
annotation class GeneralPrefs

@Module
@InstallIn(SingletonComponent::class)
object DataModule {
    @Provides
    @Singleton
    fun providePrefsFiles(
        @ApplicationContext context: Context,
    ): GeodePrefsFiles = context.geodeContainer.prefsFiles

    @Provides
    @Singleton
    @GeneralPrefs
    fun provideGeneralPrefs(prefsFiles: GeodePrefsFiles): SharedPreferences = prefsFiles.general

    @Provides
    @Singleton
    fun provideUserDataRepository(
        @ApplicationContext context: Context,
    ): UserDataRepository = context.geodeContainer.userData
}
